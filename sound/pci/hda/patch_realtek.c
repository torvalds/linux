// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for Realtek ALC codecs
 *
 * Copyright (c) 2004 Kailang Yang <kailang@realtek.com.tw>
 *                    PeiSen Hou <pshou@realtek.com.tw>
 *                    Takashi Iwai <tiwai@suse.de>
 *                    Jonathan Woithe <jwoithe@just42.net>
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/input.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/* keep halting ALC5505 DSP, for power saving */
#define HALT_REALTEK_ALC5505

/* extra amp-initialization sequence types */
enum {
	ALC_INIT_UNDEFINED,
	ALC_INIT_NONE,
	ALC_INIT_DEFAULT,
};

enum {
	ALC_HEADSET_MODE_UNKNOWN,
	ALC_HEADSET_MODE_UNPLUGGED,
	ALC_HEADSET_MODE_HEADSET,
	ALC_HEADSET_MODE_MIC,
	ALC_HEADSET_MODE_HEADPHONE,
};

enum {
	ALC_HEADSET_TYPE_UNKNOWN,
	ALC_HEADSET_TYPE_CTIA,
	ALC_HEADSET_TYPE_OMTP,
};

enum {
	ALC_KEY_MICMUTE_INDEX,
};

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

struct alc_spec {
	struct hda_gen_spec gen; /* must be at head */

	/* codec parameterization */
	struct alc_customize_define cdefine;
	unsigned int parse_flags; /* flag for snd_hda_parse_pin_defcfg() */

	/* GPIO bits */
	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;
	bool gpio_write_delay;	/* add a delay before writing gpio_data */

	/* mute LED for HP laptops, see alc269_fixup_mic_mute_hook() */
	int mute_led_polarity;
	hda_nid_t mute_led_nid;
	hda_nid_t cap_mute_led_nid;

	unsigned int gpio_mute_led_mask;
	unsigned int gpio_mic_led_mask;
	unsigned int mute_led_coef_idx;
	unsigned int mute_led_coefbit_mask;
	unsigned int mute_led_coefbit_on;
	unsigned int mute_led_coefbit_off;
	unsigned int mic_led_coef_idx;
	unsigned int mic_led_coefbit_mask;
	unsigned int mic_led_coefbit_on;
	unsigned int mic_led_coefbit_off;

	hda_nid_t headset_mic_pin;
	hda_nid_t headphone_mic_pin;
	int current_headset_mode;
	int current_headset_type;

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
#ifdef CONFIG_PM
	void (*power_hook)(struct hda_codec *codec);
#endif
	void (*shutup)(struct hda_codec *codec);
	void (*reboot_notify)(struct hda_codec *codec);

	int init_amp;
	int codec_variant;	/* flag for other variants */
	unsigned int has_alc5505_dsp:1;
	unsigned int no_depop_delay:1;
	unsigned int done_hp_init:1;
	unsigned int no_shutup_pins:1;
	unsigned int ultra_low_power:1;
	unsigned int has_hs_key:1;

	/* for PLL fix */
	hda_nid_t pll_nid;
	unsigned int pll_coef_idx, pll_coef_bit;
	unsigned int coef0;
	struct input_dev *kb_dev;
	u8 alc_mute_keycode_map[1];
};

/*
 * COEF access helper functions
 */

static int alc_read_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int coef_idx)
{
	unsigned int val;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_COEF_INDEX, coef_idx);
	val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_PROC_COEF, 0);
	return val;
}

#define alc_read_coef_idx(codec, coef_idx) \
	alc_read_coefex_idx(codec, 0x20, coef_idx)

static void alc_write_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int coef_idx, unsigned int coef_val)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_COEF_INDEX, coef_idx);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PROC_COEF, coef_val);
}

#define alc_write_coef_idx(codec, coef_idx, coef_val) \
	alc_write_coefex_idx(codec, 0x20, coef_idx, coef_val)

static void alc_update_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
				  unsigned int coef_idx, unsigned int mask,
				  unsigned int bits_set)
{
	unsigned int val = alc_read_coefex_idx(codec, nid, coef_idx);

	if (val != -1)
		alc_write_coefex_idx(codec, nid, coef_idx,
				     (val & ~mask) | bits_set);
}

#define alc_update_coef_idx(codec, coef_idx, mask, bits_set)	\
	alc_update_coefex_idx(codec, 0x20, coef_idx, mask, bits_set)

/* a special bypass for COEF 0; read the cached value at the second time */
static unsigned int alc_get_coef0(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->coef0)
		spec->coef0 = alc_read_coef_idx(codec, 0);
	return spec->coef0;
}

/* coef writes/updates batch */
struct coef_fw {
	unsigned char nid;
	unsigned char idx;
	unsigned short mask;
	unsigned short val;
};

#define UPDATE_COEFEX(_nid, _idx, _mask, _val) \
	{ .nid = (_nid), .idx = (_idx), .mask = (_mask), .val = (_val) }
#define WRITE_COEFEX(_nid, _idx, _val) UPDATE_COEFEX(_nid, _idx, -1, _val)
#define WRITE_COEF(_idx, _val) WRITE_COEFEX(0x20, _idx, _val)
#define UPDATE_COEF(_idx, _mask, _val) UPDATE_COEFEX(0x20, _idx, _mask, _val)

static void alc_process_coef_fw(struct hda_codec *codec,
				const struct coef_fw *fw)
{
	for (; fw->nid; fw++) {
		if (fw->mask == (unsigned short)-1)
			alc_write_coefex_idx(codec, fw->nid, fw->idx, fw->val);
		else
			alc_update_coefex_idx(codec, fw->nid, fw->idx,
					      fw->mask, fw->val);
	}
}

/*
 * GPIO setup tables, used in initialization
 */

/* Enable GPIO mask and set output */
static void alc_setup_gpio(struct hda_codec *codec, unsigned int mask)
{
	struct alc_spec *spec = codec->spec;

	spec->gpio_mask |= mask;
	spec->gpio_dir |= mask;
	spec->gpio_data |= mask;
}

static void alc_write_gpio_data(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
			    spec->gpio_data);
}

static void alc_update_gpio_data(struct hda_codec *codec, unsigned int mask,
				 bool on)
{
	struct alc_spec *spec = codec->spec;
	unsigned int oldval = spec->gpio_data;

	if (on)
		spec->gpio_data |= mask;
	else
		spec->gpio_data &= ~mask;
	if (oldval != spec->gpio_data)
		alc_write_gpio_data(codec);
}

static void alc_write_gpio(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->gpio_mask)
		return;

	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_MASK, spec->gpio_mask);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, spec->gpio_dir);
	if (spec->gpio_write_delay)
		msleep(1);
	alc_write_gpio_data(codec);
}

static void alc_fixup_gpio(struct hda_codec *codec, int action,
			   unsigned int mask)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		alc_setup_gpio(codec, mask);
}

static void alc_fixup_gpio1(struct hda_codec *codec,
			    const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x01);
}

static void alc_fixup_gpio2(struct hda_codec *codec,
			    const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x02);
}

static void alc_fixup_gpio3(struct hda_codec *codec,
			    const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x03);
}

static void alc_fixup_gpio4(struct hda_codec *codec,
			    const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x04);
}

/*
 * Fix hardware PLL issue
 * On some codecs, the analog PLL gating control must be off while
 * the default value is 1.
 */
static void alc_fix_pll(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->pll_nid)
		alc_update_coefex_idx(codec, spec->pll_nid, spec->pll_coef_idx,
				      1 << spec->pll_coef_bit, 0);
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

/* update the master volume per volume-knob's unsol event */
static void alc_update_knob_master(struct hda_codec *codec,
				   struct hda_jack_callback *jack)
{
	unsigned int val;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_value *uctl;

	kctl = snd_hda_find_mixer_ctl(codec, "Master Playback Volume");
	if (!kctl)
		return;
	uctl = kzalloc(sizeof(*uctl), GFP_KERNEL);
	if (!uctl)
		return;
	val = snd_hda_codec_read(codec, jack->nid, 0,
				 AC_VERB_GET_VOLUME_KNOB_CONTROL, 0);
	val &= HDA_AMP_VOLMASK;
	uctl->value.integer.value[0] = val;
	uctl->value.integer.value[1] = val;
	kctl->put(kctl, uctl);
	kfree(uctl);
}

static void alc880_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* For some reason, the res given from ALC880 is broken.
	   Here we adjust it properly. */
	snd_hda_jack_unsol_event(codec, res >> 2);
}

/* Change EAPD to verb control */
static void alc_fill_eapd_coef(struct hda_codec *codec)
{
	int coef;

	coef = alc_get_coef0(codec);

	switch (codec->core.vendor_id) {
	case 0x10ec0262:
		alc_update_coef_idx(codec, 0x7, 0, 1<<5);
		break;
	case 0x10ec0267:
	case 0x10ec0268:
		alc_update_coef_idx(codec, 0x7, 0, 1<<13);
		break;
	case 0x10ec0269:
		if ((coef & 0x00f0) == 0x0010)
			alc_update_coef_idx(codec, 0xd, 0, 1<<14);
		if ((coef & 0x00f0) == 0x0020)
			alc_update_coef_idx(codec, 0x4, 1<<15, 0);
		if ((coef & 0x00f0) == 0x0030)
			alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		break;
	case 0x10ec0280:
	case 0x10ec0284:
	case 0x10ec0290:
	case 0x10ec0292:
		alc_update_coef_idx(codec, 0x4, 1<<15, 0);
		break;
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_update_coef_idx(codec, 0x67, 0xf000, 0x3000);
		/* fallthrough */
	case 0x10ec0215:
	case 0x10ec0233:
	case 0x10ec0235:
	case 0x10ec0236:
	case 0x10ec0245:
	case 0x10ec0255:
	case 0x10ec0256:
	case 0x10ec0257:
	case 0x10ec0282:
	case 0x10ec0283:
	case 0x10ec0286:
	case 0x10ec0287:
	case 0x10ec0288:
	case 0x10ec0285:
	case 0x10ec0298:
	case 0x10ec0289:
	case 0x10ec0300:
		alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		break;
	case 0x10ec0275:
		alc_update_coef_idx(codec, 0xe, 0, 1<<0);
		break;
	case 0x10ec0293:
		alc_update_coef_idx(codec, 0xa, 1<<13, 0);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
	case 0x10ec0700:
	case 0x10ec0701:
	case 0x10ec0703:
	case 0x10ec0711:
		alc_update_coef_idx(codec, 0x10, 1<<15, 0);
		break;
	case 0x10ec0662:
		if ((coef & 0x00f0) == 0x0030)
			alc_update_coef_idx(codec, 0x4, 1<<10, 0); /* EAPD Ctrl */
		break;
	case 0x10ec0272:
	case 0x10ec0273:
	case 0x10ec0663:
	case 0x10ec0665:
	case 0x10ec0670:
	case 0x10ec0671:
	case 0x10ec0672:
		alc_update_coef_idx(codec, 0xd, 0, 1<<14); /* EAPD Ctrl */
		break;
	case 0x10ec0222:
	case 0x10ec0623:
		alc_update_coef_idx(codec, 0x19, 1<<13, 0);
		break;
	case 0x10ec0668:
		alc_update_coef_idx(codec, 0x7, 3<<13, 0);
		break;
	case 0x10ec0867:
		alc_update_coef_idx(codec, 0x4, 1<<10, 0);
		break;
	case 0x10ec0888:
		if ((coef & 0x00f0) == 0x0020 || (coef & 0x00f0) == 0x0030)
			alc_update_coef_idx(codec, 0x7, 1<<5, 0);
		break;
	case 0x10ec0892:
		alc_update_coef_idx(codec, 0x7, 1<<5, 0);
		break;
	case 0x10ec0899:
	case 0x10ec0900:
	case 0x10ec0b00:
	case 0x10ec1168:
	case 0x10ec1220:
		alc_update_coef_idx(codec, 0x7, 1<<1, 0);
		break;
	}
}

/* additional initialization for ALC888 variants */
static void alc888_coef_init(struct hda_codec *codec)
{
	switch (alc_get_coef0(codec) & 0x00f0) {
	/* alc888-VA */
	case 0x00:
	/* alc888-VB */
	case 0x10:
		alc_update_coef_idx(codec, 7, 0, 0x2030); /* Turn EAPD to High */
		break;
	}
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
	static const hda_nid_t pins[] = {
		0x0f, 0x10, 0x14, 0x15, 0x17, 0
	};
	const hda_nid_t *p;
	for (p = pins; *p; p++)
		set_eapd(codec, *p, on);
}

static int find_ext_mic_pin(struct hda_codec *codec);

static void alc_headset_mic_no_shutup(struct hda_codec *codec)
{
	const struct hda_pincfg *pin;
	int mic_pin = find_ext_mic_pin(codec);
	int i;

	/* don't shut up pins when unloading the driver; otherwise it breaks
	 * the default pin setup at the next load of the driver
	 */
	if (codec->bus->shutdown)
		return;

	snd_array_for_each(&codec->init_pins, i, pin) {
		/* use read here for syncing after issuing each verb */
		if (pin->nid != mic_pin)
			snd_hda_codec_read(codec, pin->nid, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
	}

	codec->pins_shutup = 1;
}

static void alc_shutup_pins(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	switch (codec->core.vendor_id) {
	case 0x10ec0283:
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0298:
		alc_headset_mic_no_shutup(codec);
		break;
	default:
		if (!spec->no_shutup_pins)
			snd_hda_shutup_pins(codec);
		break;
	}
}

/* generic shutup callback;
 * just turning off EAPD and a little pause for avoiding pop-noise
 */
static void alc_eapd_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	alc_auto_setup_eapd(codec, false);
	if (!spec->no_depop_delay)
		msleep(200);
	alc_shutup_pins(codec);
}

/* generic EAPD initialization */
static void alc_auto_init_amp(struct hda_codec *codec, int type)
{
	alc_auto_setup_eapd(codec, true);
	alc_write_gpio(codec);
	switch (type) {
	case ALC_INIT_DEFAULT:
		switch (codec->core.vendor_id) {
		case 0x10ec0260:
			alc_update_coefex_idx(codec, 0x1a, 7, 0, 0x2010);
			break;
		case 0x10ec0880:
		case 0x10ec0882:
		case 0x10ec0883:
		case 0x10ec0885:
			alc_update_coef_idx(codec, 7, 0, 0x2030);
			break;
		case 0x10ec0888:
			alc888_coef_init(codec);
			break;
		}
		break;
	}
}

/* get a primary headphone pin if available */
static hda_nid_t alc_get_hp_pin(struct alc_spec *spec)
{
	if (spec->gen.autocfg.hp_pins[0])
		return spec->gen.autocfg.hp_pins[0];
	if (spec->gen.autocfg.line_out_type == AC_JACK_HP_OUT)
		return spec->gen.autocfg.line_out_pins[0];
	return 0;
}

/*
 * Realtek SSID verification
 */

/* Could be any non-zero and even value. When used as fixup, tells
 * the driver to ignore any present sku defines.
 */
#define ALC_FIXUP_SKU_IGNORE (2)

static void alc_fixup_sku_ignore(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->cdefine.fixup = 1;
		spec->cdefine.sku_cfg = ALC_FIXUP_SKU_IGNORE;
	}
}

static void alc_fixup_no_depop_delay(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		spec->no_depop_delay = 1;
		codec->depop_delay = 0;
	}
}

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

	if (!codec->bus->pci)
		return -1;
	ass = codec->core.subsystem_id & 0xffff;
	if (ass != codec->bus->pci->subsystem_device && (ass & 1))
		goto do_sku;

	nid = 0x1d;
	if (codec->core.vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);

	if (!(ass & 1)) {
		codec_info(codec, "%s: SKU not ready 0x%08x\n",
			   codec->core.chip_name, ass);
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

	codec_dbg(codec, "SKU: Nid=0x%x sku_cfg=0x%08x\n",
		   nid, spec->cdefine.sku_cfg);
	codec_dbg(codec, "SKU: port_connectivity=0x%x\n",
		   spec->cdefine.port_connectivity);
	codec_dbg(codec, "SKU: enable_pcbeep=0x%x\n", spec->cdefine.enable_pcbeep);
	codec_dbg(codec, "SKU: check_sum=0x%08x\n", spec->cdefine.check_sum);
	codec_dbg(codec, "SKU: customization=0x%08x\n", spec->cdefine.customization);
	codec_dbg(codec, "SKU: external_amp=0x%x\n", spec->cdefine.external_amp);
	codec_dbg(codec, "SKU: platform_type=0x%x\n", spec->cdefine.platform_type);
	codec_dbg(codec, "SKU: swap=0x%x\n", spec->cdefine.swap);
	codec_dbg(codec, "SKU: override=0x%x\n", spec->cdefine.override);

	return 0;
}

/* return the position of NID in the list, or -1 if not found */
static int find_idx_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	int i;
	for (i = 0; i < nums; i++)
		if (list[i] == nid)
			return i;
	return -1;
}
/* return true if the given NID is found in the list */
static bool found_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	return find_idx_in_nid_list(nid, list, nums) >= 0;
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
static int alc_subsystem_id(struct hda_codec *codec, const hda_nid_t *ports)
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

	ass = codec->core.subsystem_id & 0xffff;
	if (codec->bus->pci &&
	    ass != codec->bus->pci->subsystem_device && (ass & 1))
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
	if (codec->core.vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);
	codec_dbg(codec,
		  "realtek: No valid SSID, checking pincfg 0x%08x for NID 0x%x\n",
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
	codec_dbg(codec, "realtek: Enabling init ASM_ID=0x%04x CODEC_ID=%08x\n",
		   ass & 0xffff, codec->core.vendor_id);
	/*
	 * 0 : override
	 * 1 :	Swap Jack
	 * 2 : 0 --> Desktop, 1 --> Laptop
	 * 3~5 : External Amplifier control
	 * 7~6 : Reserved
	*/
	tmp = (ass & 0x38) >> 3;	/* external Amp control */
	if (spec->init_amp == ALC_INIT_UNDEFINED) {
		switch (tmp) {
		case 1:
			alc_setup_gpio(codec, 0x01);
			break;
		case 3:
			alc_setup_gpio(codec, 0x02);
			break;
		case 7:
			alc_setup_gpio(codec, 0x03);
			break;
		case 5:
		default:
			spec->init_amp = ALC_INIT_DEFAULT;
			break;
		}
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
	if (!alc_get_hp_pin(spec)) {
		hda_nid_t nid;
		tmp = (ass >> 11) & 0x3;	/* HP to chassis */
		nid = ports[tmp];
		if (found_in_nid_list(nid, spec->gen.autocfg.line_out_pins,
				      spec->gen.autocfg.line_outs))
			return 1;
		spec->gen.autocfg.hp_pins[0] = nid;
	}
	return 1;
}

/* Check the validity of ALC subsystem-id
 * ports contains an array of 4 pin NIDs for port-A, E, D and I */
static void alc_ssid_check(struct hda_codec *codec, const hda_nid_t *ports)
{
	if (!alc_subsystem_id(codec, ports)) {
		struct alc_spec *spec = codec->spec;
		if (spec->init_amp == ALC_INIT_UNDEFINED) {
			codec_dbg(codec,
				  "realtek: Enable default setup for auto mode as fallback\n");
			spec->init_amp = ALC_INIT_DEFAULT;
		}
	}
}

/*
 */

static void alc_fixup_inv_dmic(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	spec->gen.inv_dmic_split = 1;
}


static int alc_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);
	return 0;
}


/*
 * Common callbacks
 */

static void alc_pre_init(struct hda_codec *codec)
{
	alc_fill_eapd_coef(codec);
}

#define is_s3_resume(codec) \
	((codec)->core.dev.power.power_state.event == PM_EVENT_RESUME)
#define is_s4_resume(codec) \
	((codec)->core.dev.power.power_state.event == PM_EVENT_RESTORE)

static int alc_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	/* hibernation resume needs the full chip initialization */
	if (is_s4_resume(codec))
		alc_pre_init(codec);

	if (spec->init_hook)
		spec->init_hook(codec);

	spec->gen.skip_verbs = 1; /* applied in below */
	snd_hda_gen_init(codec);
	alc_fix_pll(codec);
	alc_auto_init_amp(codec, spec->init_amp);
	snd_hda_apply_verbs(codec); /* apply verbs here after own init */

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return 0;
}

static inline void alc_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!snd_hda_get_bool_hint(codec, "shutup"))
		return; /* disabled explicitly by hints */

	if (spec && spec->shutup)
		spec->shutup(codec);
	else
		alc_shutup_pins(codec);
}

static void alc_reboot_notify(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec && spec->reboot_notify)
		spec->reboot_notify(codec);
	else
		alc_shutup(codec);
}

#define alc_free	snd_hda_gen_free

#ifdef CONFIG_PM
static void alc_power_eapd(struct hda_codec *codec)
{
	alc_auto_setup_eapd(codec, false);
}

static int alc_suspend(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc_shutup(codec);
	if (spec && spec->power_hook)
		spec->power_hook(codec);
	return 0;
}
#endif

#ifdef CONFIG_PM
static int alc_resume(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->no_depop_delay)
		msleep(150); /* to avoid pop noise */
	codec->patch_ops.init(codec);
	snd_hda_regmap_sync(codec);
	hda_call_check_power_status(codec, 0x01);
	return 0;
}
#endif

/*
 */
static const struct hda_codec_ops alc_patch_ops = {
	.build_controls = alc_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = alc_init,
	.free = alc_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.resume = alc_resume,
	.suspend = alc_suspend,
	.check_power_status = snd_hda_gen_check_power_status,
#endif
	.reboot_notify = alc_reboot_notify,
};


#define alc_codec_rename(codec, name) snd_hda_codec_set_name(codec, name)

/*
 * Rename codecs appropriately from COEF value or subvendor id
 */
struct alc_codec_rename_table {
	unsigned int vendor_id;
	unsigned short coef_mask;
	unsigned short coef_bits;
	const char *name;
};

struct alc_codec_rename_pci_table {
	unsigned int codec_vendor_id;
	unsigned short pci_subvendor;
	unsigned short pci_subdevice;
	const char *name;
};

static const struct alc_codec_rename_table rename_tbl[] = {
	{ 0x10ec0221, 0xf00f, 0x1003, "ALC231" },
	{ 0x10ec0269, 0xfff0, 0x3010, "ALC277" },
	{ 0x10ec0269, 0xf0f0, 0x2010, "ALC259" },
	{ 0x10ec0269, 0xf0f0, 0x3010, "ALC258" },
	{ 0x10ec0269, 0x00f0, 0x0010, "ALC269VB" },
	{ 0x10ec0269, 0xffff, 0xa023, "ALC259" },
	{ 0x10ec0269, 0xffff, 0x6023, "ALC281X" },
	{ 0x10ec0269, 0x00f0, 0x0020, "ALC269VC" },
	{ 0x10ec0269, 0x00f0, 0x0030, "ALC269VD" },
	{ 0x10ec0662, 0xffff, 0x4020, "ALC656" },
	{ 0x10ec0887, 0x00f0, 0x0030, "ALC887-VD" },
	{ 0x10ec0888, 0x00f0, 0x0030, "ALC888-VD" },
	{ 0x10ec0888, 0xf0f0, 0x3020, "ALC886" },
	{ 0x10ec0899, 0x2000, 0x2000, "ALC899" },
	{ 0x10ec0892, 0xffff, 0x8020, "ALC661" },
	{ 0x10ec0892, 0xffff, 0x8011, "ALC661" },
	{ 0x10ec0892, 0xffff, 0x4011, "ALC656" },
	{ } /* terminator */
};

static const struct alc_codec_rename_pci_table rename_pci_tbl[] = {
	{ 0x10ec0280, 0x1028, 0, "ALC3220" },
	{ 0x10ec0282, 0x1028, 0, "ALC3221" },
	{ 0x10ec0283, 0x1028, 0, "ALC3223" },
	{ 0x10ec0288, 0x1028, 0, "ALC3263" },
	{ 0x10ec0292, 0x1028, 0, "ALC3226" },
	{ 0x10ec0293, 0x1028, 0, "ALC3235" },
	{ 0x10ec0255, 0x1028, 0, "ALC3234" },
	{ 0x10ec0668, 0x1028, 0, "ALC3661" },
	{ 0x10ec0275, 0x1028, 0, "ALC3260" },
	{ 0x10ec0899, 0x1028, 0, "ALC3861" },
	{ 0x10ec0298, 0x1028, 0, "ALC3266" },
	{ 0x10ec0236, 0x1028, 0, "ALC3204" },
	{ 0x10ec0256, 0x1028, 0, "ALC3246" },
	{ 0x10ec0225, 0x1028, 0, "ALC3253" },
	{ 0x10ec0295, 0x1028, 0, "ALC3254" },
	{ 0x10ec0299, 0x1028, 0, "ALC3271" },
	{ 0x10ec0670, 0x1025, 0, "ALC669X" },
	{ 0x10ec0676, 0x1025, 0, "ALC679X" },
	{ 0x10ec0282, 0x1043, 0, "ALC3229" },
	{ 0x10ec0233, 0x1043, 0, "ALC3236" },
	{ 0x10ec0280, 0x103c, 0, "ALC3228" },
	{ 0x10ec0282, 0x103c, 0, "ALC3227" },
	{ 0x10ec0286, 0x103c, 0, "ALC3242" },
	{ 0x10ec0290, 0x103c, 0, "ALC3241" },
	{ 0x10ec0668, 0x103c, 0, "ALC3662" },
	{ 0x10ec0283, 0x17aa, 0, "ALC3239" },
	{ 0x10ec0292, 0x17aa, 0, "ALC3232" },
	{ } /* terminator */
};

static int alc_codec_rename_from_preset(struct hda_codec *codec)
{
	const struct alc_codec_rename_table *p;
	const struct alc_codec_rename_pci_table *q;

	for (p = rename_tbl; p->vendor_id; p++) {
		if (p->vendor_id != codec->core.vendor_id)
			continue;
		if ((alc_get_coef0(codec) & p->coef_mask) == p->coef_bits)
			return alc_codec_rename(codec, p->name);
	}

	if (!codec->bus->pci)
		return 0;
	for (q = rename_pci_tbl; q->codec_vendor_id; q++) {
		if (q->codec_vendor_id != codec->core.vendor_id)
			continue;
		if (q->pci_subvendor != codec->bus->pci->subsystem_vendor)
			continue;
		if (!q->pci_subdevice ||
		    q->pci_subdevice == codec->bus->pci->subsystem_device)
			return alc_codec_rename(codec, q->name);
	}

	return 0;
}


/*
 * Digital-beep handlers
 */
#ifdef CONFIG_SND_HDA_INPUT_BEEP

/* additional beep mixers; private_value will be overwritten */
static const struct snd_kcontrol_new alc_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0, 0, HDA_INPUT),
	HDA_CODEC_MUTE_BEEP("Beep Playback Switch", 0, 0, HDA_INPUT),
};

/* set up and create beep controls */
static int set_beep_amp(struct alc_spec *spec, hda_nid_t nid,
			int idx, int dir)
{
	struct snd_kcontrol_new *knew;
	unsigned int beep_amp = HDA_COMPOSE_AMP_VAL(nid, 3, idx, dir);
	int i;

	for (i = 0; i < ARRAY_SIZE(alc_beep_mixer); i++) {
		knew = snd_hda_gen_add_kctl(&spec->gen, NULL,
					    &alc_beep_mixer[i]);
		if (!knew)
			return -ENOMEM;
		knew->private_value = beep_amp;
	}
	return 0;
}

static const struct snd_pci_quirk beep_white_list[] = {
	SND_PCI_QUIRK(0x1043, 0x103c, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x115d, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x829f, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x8376, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x83ce, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x831a, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x834a, "EeePC", 1),
	SND_PCI_QUIRK(0x1458, 0xa002, "GA-MA790X", 1),
	SND_PCI_QUIRK(0x8086, 0xd613, "Intel", 1),
	/* blacklist -- no beep available */
	SND_PCI_QUIRK(0x17aa, 0x309e, "Lenovo ThinkCentre M73", 0),
	SND_PCI_QUIRK(0x17aa, 0x30a3, "Lenovo ThinkCentre M93", 0),
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
#define set_beep_amp(spec, nid, idx, dir)	0
#define has_cdefine_beep(codec)		0
#endif

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
static int alc_parse_auto_config(struct hda_codec *codec,
				 const hda_nid_t *ignore_nids,
				 const hda_nid_t *ssid_nids)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	int err;

	err = snd_hda_parse_pin_defcfg(codec, cfg, ignore_nids,
				       spec->parse_flags);
	if (err < 0)
		return err;

	if (ssid_nids)
		alc_ssid_check(codec, ssid_nids);

	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		return err;

	return 1;
}

/* common preparation job for alc_spec */
static int alc_alloc_spec(struct hda_codec *codec, hda_nid_t mixer_nid)
{
	struct alc_spec *spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	int err;

	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	snd_hda_gen_spec_init(&spec->gen);
	spec->gen.mixer_nid = mixer_nid;
	spec->gen.own_eapd_ctl = 1;
	codec->single_adc_amp = 1;
	/* FIXME: do we need this for all Realtek codec models? */
	codec->spdif_status_reset = 1;
	codec->patch_ops = alc_patch_ops;

	err = alc_codec_rename_from_preset(codec);
	if (err < 0) {
		kfree(spec);
		return err;
	}
	return 0;
}

static int alc880_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc880_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc880_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc880_ignore, alc880_ssids);
}

/*
 * ALC880 fix-ups
 */
enum {
	ALC880_FIXUP_GPIO1,
	ALC880_FIXUP_GPIO2,
	ALC880_FIXUP_MEDION_RIM,
	ALC880_FIXUP_LG,
	ALC880_FIXUP_LG_LW25,
	ALC880_FIXUP_W810,
	ALC880_FIXUP_EAPD_COEF,
	ALC880_FIXUP_TCL_S700,
	ALC880_FIXUP_VOL_KNOB,
	ALC880_FIXUP_FUJITSU,
	ALC880_FIXUP_F1734,
	ALC880_FIXUP_UNIWILL,
	ALC880_FIXUP_UNIWILL_DIG,
	ALC880_FIXUP_Z71V,
	ALC880_FIXUP_ASUS_W5A,
	ALC880_FIXUP_3ST_BASE,
	ALC880_FIXUP_3ST,
	ALC880_FIXUP_3ST_DIG,
	ALC880_FIXUP_5ST_BASE,
	ALC880_FIXUP_5ST,
	ALC880_FIXUP_5ST_DIG,
	ALC880_FIXUP_6ST_BASE,
	ALC880_FIXUP_6ST,
	ALC880_FIXUP_6ST_DIG,
	ALC880_FIXUP_6ST_AUTOMUTE,
};

/* enable the volume-knob widget support on NID 0x21 */
static void alc880_fixup_vol_knob(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PROBE)
		snd_hda_jack_detect_enable_callback(codec, 0x21,
						    alc_update_knob_master);
}

static const struct hda_fixup alc880_fixups[] = {
	[ALC880_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
	},
	[ALC880_FIXUP_GPIO2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio2,
	},
	[ALC880_FIXUP_MEDION_RIM] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_LG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x16, 0x411111f0 },
			{ 0x18, 0x411111f0 },
			{ 0x1a, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_LG_LW25] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x0181344f }, /* line-in */
			{ 0x1b, 0x0321403f }, /* headphone */
			{ }
		}
	},
	[ALC880_FIXUP_W810] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_EAPD_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{}
		},
	},
	[ALC880_FIXUP_TCL_S700] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3070 },
			{}
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_VOL_KNOB] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc880_fixup_vol_knob,
	},
	[ALC880_FIXUP_FUJITSU] = {
		/* override all pins as BIOS on old Amilo is broken */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121401f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x01454140 }, /* SPDIF out */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_F1734] = {
		/* almost compatible with FUJITSU, but no bass and SPDIF */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121401f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_UNIWILL] = {
		/* need to fix HP and speaker pins to be parsed correctly */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ }
		},
	},
	[ALC880_FIXUP_UNIWILL_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ 0x19, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1f, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_Z71V] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set up the whole pins as BIOS is utterly broken */
			{ 0x14, 0x99030120 }, /* speaker */
			{ 0x15, 0x0121411f }, /* HP */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19950 }, /* mic-in */
			{ 0x19, 0x411111f0 }, /* N/A */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		}
	},
	[ALC880_FIXUP_ASUS_W5A] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set up the whole pins as BIOS is utterly broken */
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x90a60160 }, /* mic */
			{ 0x19, 0x411111f0 }, /* N/A */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0xb743111e }, /* SPDIF out */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO1,
	},
	[ALC880_FIXUP_3ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* line-out */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_3ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_3ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_5ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01016412 }, /* surr */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_5ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_5ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_6ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x01016412 }, /* surr */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01012414 }, /* side */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x02a19c40 }, /* front-mic */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x0121411f }, /* HP */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_6ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
	[ALC880_FIXUP_6ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
	[ALC880_FIXUP_6ST_AUTOMUTE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x0121401f }, /* HP with jack detect */
			{ }
		},
		.chained_before = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
};

static const struct snd_pci_quirk alc880_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x0f69, "Coeus G610P", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x1043, 0x10c3, "ASUS W5A", ALC880_FIXUP_ASUS_W5A),
	SND_PCI_QUIRK(0x1043, 0x1964, "ASUS Z71V", ALC880_FIXUP_Z71V),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS", ALC880_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x147b, 0x1045, "ABit AA8XE", ALC880_FIXUP_6ST_AUTOMUTE),
	SND_PCI_QUIRK(0x1558, 0x5401, "Clevo GPIO2", ALC880_FIXUP_GPIO2),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo", ALC880_FIXUP_EAPD_COEF),
	SND_PCI_QUIRK(0x1584, 0x9050, "Uniwill", ALC880_FIXUP_UNIWILL_DIG),
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwill", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_FIXUP_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_FIXUP_VOL_KNOB),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x161f, 0x205d, "Medion Rim 2150", ALC880_FIXUP_MEDION_RIM),
	SND_PCI_QUIRK(0x1631, 0xe011, "PB 13201056", ALC880_FIXUP_6ST_AUTOMUTE),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC Amilo M1437", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC AMILO Xi 1526", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1734, 0x10b0, "FSC Amilo Pi1556", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x005f, "LG P1 Express", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x0068, "LG w1", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x0077, "LG LW25", ALC880_FIXUP_LG_LW25),
	SND_PCI_QUIRK(0x19db, 0x4188, "TCL S700", ALC880_FIXUP_TCL_S700),

	/* Below is the copied entries from alc880_quirks.c.
	 * It's not quite sure whether BIOS sets the correct pin-config table
	 * on these machines, thus they are kept to be compatible with
	 * the old static quirks.  Once when it's confirmed to work without
	 * these overrides, it'd be better to remove.
	 */
	SND_PCI_QUIRK(0x1019, 0xa880, "ECS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1019, 0xa884, "Acer APFV", ALC880_FIXUP_6ST),
	SND_PCI_QUIRK(0x1025, 0x0070, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0077, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0078, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0087, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe309, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe310, "ULI", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x1039, 0x1234, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x104d, 0x81a0, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x104d, 0x81d6, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x107b, 0x3032, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x3033, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x4039, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x1297, 0xc790, "Shuttle ST20G5", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa102, "Gigabyte K8", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x1150, "MSI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1509, 0x925d, "FIC P4M", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1565, 0x8202, "Biostar", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x2668, 0x8086, NULL, ALC880_FIXUP_6ST_DIG), /* broken BIOS */
	SND_PCI_QUIRK(0x8086, 0x2668, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xa100, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd402, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe224, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe305, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe308, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe402, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	/* default Intel */
	SND_PCI_QUIRK_VENDOR(0x8086, "Intel mobo", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_FIXUP_6ST_DIG),
	{}
};

static const struct hda_model_fixup alc880_fixup_models[] = {
	{.id = ALC880_FIXUP_3ST, .name = "3stack"},
	{.id = ALC880_FIXUP_3ST_DIG, .name = "3stack-digout"},
	{.id = ALC880_FIXUP_5ST, .name = "5stack"},
	{.id = ALC880_FIXUP_5ST_DIG, .name = "5stack-digout"},
	{.id = ALC880_FIXUP_6ST, .name = "6stack"},
	{.id = ALC880_FIXUP_6ST_DIG, .name = "6stack-digout"},
	{.id = ALC880_FIXUP_6ST_AUTOMUTE, .name = "6stack-automute"},
	{}
};


/*
 * OK, here we have finally the patch for ALC880
 */
static int patch_alc880(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.need_dac_fix = 1;
	spec->gen.beep_nid = 0x01;

	codec->patch_ops.unsol_event = alc880_unsol_event;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc880_fixup_models, alc880_fixup_tbl,
		       alc880_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc880_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog) {
		err = set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}


/*
 * ALC260 support
 */
static int alc260_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc260_ignore[] = { 0x17, 0 };
	static const hda_nid_t alc260_ssids[] = { 0x10, 0x15, 0x0f, 0 };
	return alc_parse_auto_config(codec, alc260_ignore, alc260_ssids);
}

/*
 * Pin config fixes
 */
enum {
	ALC260_FIXUP_HP_DC5750,
	ALC260_FIXUP_HP_PIN_0F,
	ALC260_FIXUP_COEF,
	ALC260_FIXUP_GPIO1,
	ALC260_FIXUP_GPIO1_TOGGLE,
	ALC260_FIXUP_REPLACER,
	ALC260_FIXUP_HP_B1900,
	ALC260_FIXUP_KN1,
	ALC260_FIXUP_FSC_S7020,
	ALC260_FIXUP_FSC_S7020_JWSE,
	ALC260_FIXUP_VAIO_PINS,
};

static void alc260_gpio1_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	alc_update_gpio_data(codec, 0x01, spec->gen.hp_jack_present);
}

static void alc260_fixup_gpio1_toggle(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PROBE) {
		/* although the machine has only one output pin, we need to
		 * toggle GPIO1 according to the jack state
		 */
		spec->gen.automute_hook = alc260_gpio1_automute;
		spec->gen.detect_hp = 1;
		spec->gen.automute_speaker = 1;
		spec->gen.autocfg.hp_pins[0] = 0x0f; /* copy it for automute */
		snd_hda_jack_detect_enable_callback(codec, 0x0f,
						    snd_hda_gen_hp_automute);
		alc_setup_gpio(codec, 0x01);
	}
}

static void alc260_fixup_kn1(struct hda_codec *codec,
			     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_pintbl pincfgs[] = {
		{ 0x0f, 0x02214000 }, /* HP/speaker */
		{ 0x12, 0x90a60160 }, /* int mic */
		{ 0x13, 0x02a19000 }, /* ext mic */
		{ 0x18, 0x01446000 }, /* SPDIF out */
		/* disable bogus I/O pins */
		{ 0x10, 0x411111f0 },
		{ 0x11, 0x411111f0 },
		{ 0x14, 0x411111f0 },
		{ 0x15, 0x411111f0 },
		{ 0x16, 0x411111f0 },
		{ 0x17, 0x411111f0 },
		{ 0x19, 0x411111f0 },
		{ }
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		spec->init_amp = ALC_INIT_NONE;
		break;
	}
}

static void alc260_fixup_fsc_s7020(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->init_amp = ALC_INIT_NONE;
}

static void alc260_fixup_fsc_s7020_jwse(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.add_jack_modes = 1;
		spec->gen.hp_mic = 1;
	}
}

static const struct hda_fixup alc260_fixups[] = {
	[ALC260_FIXUP_HP_DC5750] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x11, 0x90130110 }, /* speaker */
			{ }
		}
	},
	[ALC260_FIXUP_HP_PIN_0F] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x0f, 0x01214000 }, /* HP */
			{ }
		}
	},
	[ALC260_FIXUP_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x1a, AC_VERB_SET_PROC_COEF,  0x3040 },
			{ }
		},
	},
	[ALC260_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
	},
	[ALC260_FIXUP_GPIO1_TOGGLE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_HP_PIN_0F,
	},
	[ALC260_FIXUP_REPLACER] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x1a, AC_VERB_SET_PROC_COEF,  0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC260_FIXUP_GPIO1_TOGGLE,
	},
	[ALC260_FIXUP_HP_B1900] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_COEF,
	},
	[ALC260_FIXUP_KN1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_kn1,
	},
	[ALC260_FIXUP_FSC_S7020] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_fsc_s7020,
	},
	[ALC260_FIXUP_FSC_S7020_JWSE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_fsc_s7020_jwse,
		.chained = true,
		.chain_id = ALC260_FIXUP_FSC_S7020,
	},
	[ALC260_FIXUP_VAIO_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* Pin configs are missing completely on some VAIOs */
			{ 0x0f, 0x01211020 },
			{ 0x10, 0x0001003f },
			{ 0x11, 0x411111f0 },
			{ 0x12, 0x01a15930 },
			{ 0x13, 0x411111f0 },
			{ 0x14, 0x411111f0 },
			{ 0x15, 0x411111f0 },
			{ 0x16, 0x411111f0 },
			{ 0x17, 0x411111f0 },
			{ 0x18, 0x411111f0 },
			{ 0x19, 0x411111f0 },
			{ }
		}
	},
};

static const struct snd_pci_quirk alc260_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x007b, "Acer C20x", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x1025, 0x007f, "Acer Aspire 9500", ALC260_FIXUP_COEF),
	SND_PCI_QUIRK(0x1025, 0x008f, "Acer", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x103c, 0x280a, "HP dc5750", ALC260_FIXUP_HP_DC5750),
	SND_PCI_QUIRK(0x103c, 0x30ba, "HP Presario B1900", ALC260_FIXUP_HP_B1900),
	SND_PCI_QUIRK(0x104d, 0x81bb, "Sony VAIO", ALC260_FIXUP_VAIO_PINS),
	SND_PCI_QUIRK(0x104d, 0x81e2, "Sony VAIO TX", ALC260_FIXUP_HP_PIN_0F),
	SND_PCI_QUIRK(0x10cf, 0x1326, "FSC LifeBook S7020", ALC260_FIXUP_FSC_S7020),
	SND_PCI_QUIRK(0x1509, 0x4540, "Favorit 100XS", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x152d, 0x0729, "Quanta KN1", ALC260_FIXUP_KN1),
	SND_PCI_QUIRK(0x161f, 0x2057, "Replacer 672V", ALC260_FIXUP_REPLACER),
	SND_PCI_QUIRK(0x1631, 0xc017, "PB V7900", ALC260_FIXUP_COEF),
	{}
};

static const struct hda_model_fixup alc260_fixup_models[] = {
	{.id = ALC260_FIXUP_GPIO1, .name = "gpio1"},
	{.id = ALC260_FIXUP_COEF, .name = "coef"},
	{.id = ALC260_FIXUP_FSC_S7020, .name = "fujitsu"},
	{.id = ALC260_FIXUP_FSC_S7020_JWSE, .name = "fujitsu-jwse"},
	{}
};

/*
 */
static int patch_alc260(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x07);
	if (err < 0)
		return err;

	spec = codec->spec;
	/* as quite a few machines require HP amp for speaker outputs,
	 * it's easier to enable it unconditionally; even if it's unneeded,
	 * it's almost harmless.
	 */
	spec->gen.prefer_hp_amp = 1;
	spec->gen.beep_nid = 0x01;

	spec->shutup = alc_eapd_shutup;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc260_fixup_models, alc260_fixup_tbl,
			   alc260_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc260_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog) {
		err = set_beep_amp(spec, 0x07, 0x05, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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

/*
 * Pin config fixes
 */
enum {
	ALC882_FIXUP_ABIT_AW9D_MAX,
	ALC882_FIXUP_LENOVO_Y530,
	ALC882_FIXUP_PB_M5210,
	ALC882_FIXUP_ACER_ASPIRE_7736,
	ALC882_FIXUP_ASUS_W90V,
	ALC889_FIXUP_CD,
	ALC889_FIXUP_FRONT_HP_NO_PRESENCE,
	ALC889_FIXUP_VAIO_TT,
	ALC888_FIXUP_EEE1601,
	ALC882_FIXUP_EAPD,
	ALC883_FIXUP_EAPD,
	ALC883_FIXUP_ACER_EAPD,
	ALC882_FIXUP_GPIO1,
	ALC882_FIXUP_GPIO2,
	ALC882_FIXUP_GPIO3,
	ALC889_FIXUP_COEF,
	ALC882_FIXUP_ASUS_W2JC,
	ALC882_FIXUP_ACER_ASPIRE_4930G,
	ALC882_FIXUP_ACER_ASPIRE_8930G,
	ALC882_FIXUP_ASPIRE_8930G_VERBS,
	ALC885_FIXUP_MACPRO_GPIO,
	ALC889_FIXUP_DAC_ROUTE,
	ALC889_FIXUP_MBP_VREF,
	ALC889_FIXUP_IMAC91_VREF,
	ALC889_FIXUP_MBA11_VREF,
	ALC889_FIXUP_MBA21_VREF,
	ALC889_FIXUP_MP11_VREF,
	ALC889_FIXUP_MP41_VREF,
	ALC882_FIXUP_INV_DMIC,
	ALC882_FIXUP_NO_PRIMARY_HP,
	ALC887_FIXUP_ASUS_BASS,
	ALC887_FIXUP_BASS_CHMAP,
	ALC1220_FIXUP_GB_DUAL_CODECS,
	ALC1220_FIXUP_CLEVO_P950,
	ALC1220_FIXUP_CLEVO_PB51ED,
	ALC1220_FIXUP_CLEVO_PB51ED_PINS,
};

static void alc889_fixup_coef(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_INIT)
		return;
	alc_update_coef_idx(codec, 7, 0, 0x2030);
}

/* set up GPIO at initialization */
static void alc885_fixup_macpro_gpio(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	spec->gpio_write_delay = true;
	alc_fixup_gpio3(codec, fix, action);
}

/* Fix the connection of some pins for ALC889:
 * At least, Acer Aspire 5935 shows the connections to DAC3/4 don't
 * work correctly (bko#42740)
 */
static void alc889_fixup_dac_route(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* fake the connections during parsing the tree */
		static const hda_nid_t conn1[] = { 0x0c, 0x0d };
		static const hda_nid_t conn2[] = { 0x0e, 0x0f };
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn1), conn1);
		snd_hda_override_conn_list(codec, 0x15, ARRAY_SIZE(conn1), conn1);
		snd_hda_override_conn_list(codec, 0x18, ARRAY_SIZE(conn2), conn2);
		snd_hda_override_conn_list(codec, 0x1a, ARRAY_SIZE(conn2), conn2);
	} else if (action == HDA_FIXUP_ACT_PROBE) {
		/* restore the connections */
		static const hda_nid_t conn[] = { 0x0c, 0x0d, 0x0e, 0x0f, 0x26 };
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x15, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x18, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x1a, ARRAY_SIZE(conn), conn);
	}
}

/* Set VREF on HP pin */
static void alc889_fixup_mbp_vref(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x14, 0x15, 0x19 };
	struct alc_spec *spec = codec->spec;
	int i;

	if (action != HDA_FIXUP_ACT_INIT)
		return;
	for (i = 0; i < ARRAY_SIZE(nids); i++) {
		unsigned int val = snd_hda_codec_get_pincfg(codec, nids[i]);
		if (get_defcfg_device(val) != AC_JACK_HP_OUT)
			continue;
		val = snd_hda_codec_get_pin_target(codec, nids[i]);
		val |= AC_PINCTL_VREF_80;
		snd_hda_set_pin_ctl(codec, nids[i], val);
		spec->gen.keep_vref_in_automute = 1;
		break;
	}
}

static void alc889_fixup_mac_pins(struct hda_codec *codec,
				  const hda_nid_t *nids, int num_nids)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < num_nids; i++) {
		unsigned int val;
		val = snd_hda_codec_get_pin_target(codec, nids[i]);
		val |= AC_PINCTL_VREF_50;
		snd_hda_set_pin_ctl(codec, nids[i], val);
	}
	spec->gen.keep_vref_in_automute = 1;
}

/* Set VREF on speaker pins on imac91 */
static void alc889_fixup_imac91_vref(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x18, 0x1a };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Set VREF on speaker pins on mba11 */
static void alc889_fixup_mba11_vref(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x18 };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Set VREF on speaker pins on mba21 */
static void alc889_fixup_mba21_vref(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static const hda_nid_t nids[] = { 0x18, 0x19 };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Don't take HP output as primary
 * Strangely, the speaker output doesn't work on Vaio Z and some Vaio
 * all-in-one desktop PCs (for example VGC-LN51JGB) through DAC 0x05
 */
static void alc882_fixup_no_primary_hp(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.no_primary_hp = 1;
		spec->gen.no_multi_io = 1;
	}
}

static void alc_fixup_bass_chmap(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action);

/* For dual-codec configuration, we need to disable some features to avoid
 * conflicts of kctls and PCM streams
 */
static void alc_fixup_dual_codecs(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	/* disable vmaster */
	spec->gen.suppress_vmaster = 1;
	/* auto-mute and auto-mic switch don't work with multiple codecs */
	spec->gen.suppress_auto_mute = 1;
	spec->gen.suppress_auto_mic = 1;
	/* disable aamix as well */
	spec->gen.mixer_nid = 0;
	/* add location prefix to avoid conflicts */
	codec->force_pin_prefix = 1;
}

static void rename_ctl(struct hda_codec *codec, const char *oldname,
		       const char *newname)
{
	struct snd_kcontrol *kctl;

	kctl = snd_hda_find_mixer_ctl(codec, oldname);
	if (kctl)
		strcpy(kctl->id.name, newname);
}

static void alc1220_fixup_gb_dual_codecs(struct hda_codec *codec,
					 const struct hda_fixup *fix,
					 int action)
{
	alc_fixup_dual_codecs(codec, fix, action);
	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* override card longname to provide a unique UCM profile */
		strcpy(codec->card->longname, "HDAudio-Gigabyte-ALC1220DualCodecs");
		break;
	case HDA_FIXUP_ACT_BUILD:
		/* rename Capture controls depending on the codec */
		rename_ctl(codec, "Capture Volume",
			   codec->addr == 0 ?
			   "Rear-Panel Capture Volume" :
			   "Front-Panel Capture Volume");
		rename_ctl(codec, "Capture Switch",
			   codec->addr == 0 ?
			   "Rear-Panel Capture Switch" :
			   "Front-Panel Capture Switch");
		break;
	}
}

static void alc1220_fixup_clevo_p950(struct hda_codec *codec,
				     const struct hda_fixup *fix,
				     int action)
{
	static const hda_nid_t conn1[] = { 0x0c };

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	alc_update_coef_idx(codec, 0x7, 0, 0x3c3);
	/* We therefore want to make sure 0x14 (front headphone) and
	 * 0x1b (speakers) use the stereo DAC 0x02
	 */
	snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn1), conn1);
	snd_hda_override_conn_list(codec, 0x1b, ARRAY_SIZE(conn1), conn1);
}

static void alc_fixup_headset_mode_no_hp_mic(struct hda_codec *codec,
				const struct hda_fixup *fix, int action);

static void alc1220_fixup_clevo_pb51ed(struct hda_codec *codec,
				     const struct hda_fixup *fix,
				     int action)
{
	alc1220_fixup_clevo_p950(codec, fix, action);
	alc_fixup_headset_mode_no_hp_mic(codec, fix, action);
}

static const struct hda_fixup alc882_fixups[] = {
	[ALC882_FIXUP_ABIT_AW9D_MAX] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x01080104 }, /* side */
			{ 0x16, 0x01011012 }, /* rear */
			{ 0x17, 0x01016011 }, /* clfe */
			{ }
		}
	},
	[ALC882_FIXUP_LENOVO_Y530] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x99130112 }, /* rear int speakers */
			{ 0x16, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC882_FIXUP_PB_M5210] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, PIN_VREF50 },
			{}
		}
	},
	[ALC882_FIXUP_ACER_ASPIRE_7736] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_sku_ignore,
	},
	[ALC882_FIXUP_ASUS_W90V] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130110 }, /* fix sequence for CLFE */
			{ }
		}
	},
	[ALC889_FIXUP_CD] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1c, 0x993301f0 }, /* CD */
			{ }
		}
	},
	[ALC889_FIXUP_FRONT_HP_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x02214120 }, /* Front HP jack is flaky, disable jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC889_FIXUP_CD,
	},
	[ALC889_FIXUP_VAIO_TT] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170111 }, /* hidden surround speaker */
			{ }
		}
	},
	[ALC888_FIXUP_EEE1601] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x0838 },
			{ }
		}
	},
	[ALC882_FIXUP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3060 },
			{ }
		}
	},
	[ALC883_FIXUP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{ }
		}
	},
	[ALC883_FIXUP_ACER_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* eanable EAPD on Acer laptops */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		}
	},
	[ALC882_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
	},
	[ALC882_FIXUP_GPIO2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio2,
	},
	[ALC882_FIXUP_GPIO3] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio3,
	},
	[ALC882_FIXUP_ASUS_W2JC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio1,
		.chained = true,
		.chain_id = ALC882_FIXUP_EAPD,
	},
	[ALC889_FIXUP_COEF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_coef,
	},
	[ALC882_FIXUP_ACER_ASPIRE_4930G] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x17, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC882_FIXUP_ACER_ASPIRE_8930G] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x1b, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_ASPIRE_8930G_VERBS,
	},
	[ALC882_FIXUP_ASPIRE_8930G_VERBS] = {
		/* additional init verbs for Acer Aspire 8930G */
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enable all DACs */
			/* DAC DISABLE/MUTE 1? */
			/*  setting bits 1-5 disables DAC nids 0x02-0x06
			 *  apparently. Init=0x38 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x03 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DAC DISABLE/MUTE 2? */
			/*  some bit here disables the other DACs.
			 *  Init=0x4900 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x08 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DMIC fix
			 * This laptop has a stereo digital microphone.
			 * The mics are only 1cm apart which makes the stereo
			 * useless. However, either the mic or the ALC889
			 * makes the signal become a difference/sum signal
			 * instead of standard stereo, which is annoying.
			 * So instead we flip this bit which makes the
			 * codec replicate the sum signal to both channels,
			 * turning it into a normal mono mic.
			 */
			/* DMIC_CONTROL? Init value = 0x0001 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0003 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC885_FIXUP_MACPRO_GPIO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc885_fixup_macpro_gpio,
	},
	[ALC889_FIXUP_DAC_ROUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_dac_route,
	},
	[ALC889_FIXUP_MBP_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mbp_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC889_FIXUP_IMAC91_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_imac91_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC889_FIXUP_MBA11_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba11_vref,
		.chained = true,
		.chain_id = ALC889_FIXUP_MBP_VREF,
	},
	[ALC889_FIXUP_MBA21_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba21_vref,
		.chained = true,
		.chain_id = ALC889_FIXUP_MBP_VREF,
	},
	[ALC889_FIXUP_MP11_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba11_vref,
		.chained = true,
		.chain_id = ALC885_FIXUP_MACPRO_GPIO,
	},
	[ALC889_FIXUP_MP41_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mbp_vref,
		.chained = true,
		.chain_id = ALC885_FIXUP_MACPRO_GPIO,
	},
	[ALC882_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC882_FIXUP_NO_PRIMARY_HP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc882_fixup_no_primary_hp,
	},
	[ALC887_FIXUP_ASUS_BASS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x16, 0x99130130}, /* bass speaker */
			{}
		},
		.chained = true,
		.chain_id = ALC887_FIXUP_BASS_CHMAP,
	},
	[ALC887_FIXUP_BASS_CHMAP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_bass_chmap,
	},
	[ALC1220_FIXUP_GB_DUAL_CODECS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc1220_fixup_gb_dual_codecs,
	},
	[ALC1220_FIXUP_CLEVO_P950] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc1220_fixup_clevo_p950,
	},
	[ALC1220_FIXUP_CLEVO_PB51ED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc1220_fixup_clevo_pb51ed,
	},
	[ALC1220_FIXUP_CLEVO_PB51ED_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{}
		},
		.chained = true,
		.chain_id = ALC1220_FIXUP_CLEVO_PB51ED,
	},
};

static const struct snd_pci_quirk alc882_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0090, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0107, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x010a, "Acer Ferrari 5000", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x013e, "Acer Aspire 4930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x013f, "Acer Aspire 5930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0145, "Acer Aspire 8930G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0146, "Acer Aspire 6935G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x015e, "Acer Aspire 6930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0166, "Acer Aspire 6530G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0142, "Acer Aspire 7730G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0155, "Packard-Bell M5120", ALC882_FIXUP_PB_M5210),
	SND_PCI_QUIRK(0x1025, 0x021e, "Acer Aspire 5739G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0259, "Acer Aspire 5935", ALC889_FIXUP_DAC_ROUTE),
	SND_PCI_QUIRK(0x1025, 0x026b, "Acer Aspire 8940G", ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0296, "Acer Aspire 7736z", ALC882_FIXUP_ACER_ASPIRE_7736),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1873, "ASUS W90V", ALC882_FIXUP_ASUS_W90V),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_FIXUP_ASUS_W2JC),
	SND_PCI_QUIRK(0x1043, 0x835f, "Asus Eee 1601", ALC888_FIXUP_EEE1601),
	SND_PCI_QUIRK(0x1043, 0x84bc, "ASUS ET2700", ALC887_FIXUP_ASUS_BASS),
	SND_PCI_QUIRK(0x1043, 0x8691, "ASUS ROG Ranger VIII", ALC882_FIXUP_GPIO3),
	SND_PCI_QUIRK(0x104d, 0x9047, "Sony Vaio TT", ALC889_FIXUP_VAIO_TT),
	SND_PCI_QUIRK(0x104d, 0x905a, "Sony Vaio Z", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9060, "Sony Vaio VPCL14M1R", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9043, "Sony Vaio VGC-LN51JGB", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9044, "Sony VAIO AiO", ALC882_FIXUP_NO_PRIMARY_HP),

	/* All Apple entries are in codec SSIDs */
	SND_PCI_QUIRK(0x106b, 0x00a0, "MacBookPro 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a1, "Macbook", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a4, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x0c00, "Mac Pro", ALC889_FIXUP_MP11_VREF),
	SND_PCI_QUIRK(0x106b, 0x1000, "iMac 24", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2800, "AppleTV", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2c00, "MacbookPro rev3", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3000, "iMac", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3200, "iMac 7,1 Aluminum", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x106b, 0x3400, "MacBookAir 1,1", ALC889_FIXUP_MBA11_VREF),
	SND_PCI_QUIRK(0x106b, 0x3500, "MacBookAir 2,1", ALC889_FIXUP_MBA21_VREF),
	SND_PCI_QUIRK(0x106b, 0x3600, "Macbook 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3800, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3e00, "iMac 24 Aluminum", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x3f00, "Macbook 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4000, "MacbookPro 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4100, "Macmini 3,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4200, "Mac Pro 4,1/5,1", ALC889_FIXUP_MP41_VREF),
	SND_PCI_QUIRK(0x106b, 0x4300, "iMac 9,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4600, "MacbookPro 5,2", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4900, "iMac 9,1 Aluminum", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4a00, "Macbook 5,2", ALC889_FIXUP_MBA11_VREF),

	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte EP45-DS3/Z87X-UD3H", ALC889_FIXUP_FRONT_HP_NO_PRESENCE),
	SND_PCI_QUIRK(0x1458, 0xa0b8, "Gigabyte AZ370-Gaming", ALC1220_FIXUP_GB_DUAL_CODECS),
	SND_PCI_QUIRK(0x1458, 0xa0cd, "Gigabyte X570 Aorus Master", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1458, 0xa0ce, "Gigabyte X570 Aorus Xtreme", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1228, "MSI-GP63", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1275, "MSI-GL63", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1276, "MSI-GL73", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x1293, "MSI-GP65", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1462, 0x7350, "MSI-7350", ALC889_FIXUP_CD),
	SND_PCI_QUIRK(0x1462, 0xda57, "MSI Z270-Gaming", ALC1220_FIXUP_GB_DUAL_CODECS),
	SND_PCI_QUIRK_VENDOR(0x1462, "MSI", ALC882_FIXUP_GPIO3),
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", ALC882_FIXUP_ABIT_AW9D_MAX),
	SND_PCI_QUIRK(0x1558, 0x9501, "Clevo P950HR", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e1, "Clevo P95xER", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x95e2, "Clevo P950ER", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x96e1, "Clevo P960[ER][CDFN]-K", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x97e1, "Clevo P970[ER][CDFN]", ALC1220_FIXUP_CLEVO_P950),
	SND_PCI_QUIRK(0x1558, 0x65d1, "Clevo PB51[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x67d1, "Clevo PB71[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x50d3, "Clevo PC50[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x70d1, "Clevo PC70[ER][CDF]", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK(0x1558, 0x7714, "Clevo X170", ALC1220_FIXUP_CLEVO_PB51ED_PINS),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo laptop", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_FIXUP_EAPD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Y530", ALC882_FIXUP_LENOVO_Y530),
	SND_PCI_QUIRK(0x8086, 0x0022, "DX58SO", ALC889_FIXUP_COEF),
	{}
};

static const struct hda_model_fixup alc882_fixup_models[] = {
	{.id = ALC882_FIXUP_ABIT_AW9D_MAX, .name = "abit-aw9d"},
	{.id = ALC882_FIXUP_LENOVO_Y530, .name = "lenovo-y530"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_7736, .name = "acer-aspire-7736"},
	{.id = ALC882_FIXUP_ASUS_W90V, .name = "asus-w90v"},
	{.id = ALC889_FIXUP_CD, .name = "cd"},
	{.id = ALC889_FIXUP_FRONT_HP_NO_PRESENCE, .name = "no-front-hp"},
	{.id = ALC889_FIXUP_VAIO_TT, .name = "vaio-tt"},
	{.id = ALC888_FIXUP_EEE1601, .name = "eee1601"},
	{.id = ALC882_FIXUP_EAPD, .name = "alc882-eapd"},
	{.id = ALC883_FIXUP_EAPD, .name = "alc883-eapd"},
	{.id = ALC882_FIXUP_GPIO1, .name = "gpio1"},
	{.id = ALC882_FIXUP_GPIO2, .name = "gpio2"},
	{.id = ALC882_FIXUP_GPIO3, .name = "gpio3"},
	{.id = ALC889_FIXUP_COEF, .name = "alc889-coef"},
	{.id = ALC882_FIXUP_ASUS_W2JC, .name = "asus-w2jc"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_4930G, .name = "acer-aspire-4930g"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_8930G, .name = "acer-aspire-8930g"},
	{.id = ALC883_FIXUP_ACER_EAPD, .name = "acer-aspire"},
	{.id = ALC885_FIXUP_MACPRO_GPIO, .name = "macpro-gpio"},
	{.id = ALC889_FIXUP_DAC_ROUTE, .name = "dac-route"},
	{.id = ALC889_FIXUP_MBP_VREF, .name = "mbp-vref"},
	{.id = ALC889_FIXUP_IMAC91_VREF, .name = "imac91-vref"},
	{.id = ALC889_FIXUP_MBA11_VREF, .name = "mba11-vref"},
	{.id = ALC889_FIXUP_MBA21_VREF, .name = "mba21-vref"},
	{.id = ALC889_FIXUP_MP11_VREF, .name = "mp11-vref"},
	{.id = ALC889_FIXUP_MP41_VREF, .name = "mp41-vref"},
	{.id = ALC882_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC882_FIXUP_NO_PRIMARY_HP, .name = "no-primary-hp"},
	{.id = ALC887_FIXUP_ASUS_BASS, .name = "asus-bass"},
	{.id = ALC1220_FIXUP_GB_DUAL_CODECS, .name = "dual-codecs"},
	{.id = ALC1220_FIXUP_CLEVO_P950, .name = "clevo-p950"},
	{}
};

/*
 * BIOS auto configuration
 */
/* almost identical with ALC880 parser... */
static int alc882_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc882_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc882_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc882_ignore, alc882_ssids);
}

/*
 */
static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;

	switch (codec->core.vendor_id) {
	case 0x10ec0882:
	case 0x10ec0885:
	case 0x10ec0900:
	case 0x10ec0b00:
	case 0x10ec1220:
		break;
	default:
		/* ALC883 and variants */
		alc_fix_pll_init(codec, 0x20, 0x0a, 10);
		break;
	}

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc882_fixup_models, alc882_fixup_tbl,
		       alc882_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	/* automatic parse from the BIOS config */
	err = alc882_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid) {
		err = set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}


/*
 * ALC262 support
 */
static int alc262_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc262_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc262_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc262_ignore, alc262_ssids);
}

/*
 * Pin config fixes
 */
enum {
	ALC262_FIXUP_FSC_H270,
	ALC262_FIXUP_FSC_S7110,
	ALC262_FIXUP_HP_Z200,
	ALC262_FIXUP_TYAN,
	ALC262_FIXUP_LENOVO_3000,
	ALC262_FIXUP_BENQ,
	ALC262_FIXUP_BENQ_T31,
	ALC262_FIXUP_INV_DMIC,
	ALC262_FIXUP_INTEL_BAYLEYBAY,
};

static const struct hda_fixup alc262_fixups[] = {
	[ALC262_FIXUP_FSC_H270] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0221142f }, /* front HP */
			{ 0x1b, 0x0121141f }, /* rear HP */
			{ }
		}
	},
	[ALC262_FIXUP_FSC_S7110] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x90170110 }, /* speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC262_FIXUP_BENQ,
	},
	[ALC262_FIXUP_HP_Z200] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130120 }, /* internal speaker */
			{ }
		}
	},
	[ALC262_FIXUP_TYAN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x1993e1f0 }, /* int AUX */
			{ }
		}
	},
	[ALC262_FIXUP_LENOVO_3000] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, PIN_VREF50 },
			{}
		},
		.chained = true,
		.chain_id = ALC262_FIXUP_BENQ,
	},
	[ALC262_FIXUP_BENQ] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{}
		}
	},
	[ALC262_FIXUP_BENQ_T31] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{}
		}
	},
	[ALC262_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC262_FIXUP_INTEL_BAYLEYBAY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_depop_delay,
	},
};

static const struct snd_pci_quirk alc262_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x170b, "HP Z200", ALC262_FIXUP_HP_Z200),
	SND_PCI_QUIRK(0x10cf, 0x1397, "Fujitsu Lifebook S7110", ALC262_FIXUP_FSC_S7110),
	SND_PCI_QUIRK(0x10cf, 0x142d, "Fujitsu Lifebook E8410", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x10f1, 0x2915, "Tyan Thunder n6650W", ALC262_FIXUP_TYAN),
	SND_PCI_QUIRK(0x1734, 0x1141, "FSC ESPRIMO U9210", ALC262_FIXUP_FSC_H270),
	SND_PCI_QUIRK(0x1734, 0x1147, "FSC Celsius H270", ALC262_FIXUP_FSC_H270),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000", ALC262_FIXUP_LENOVO_3000),
	SND_PCI_QUIRK(0x17ff, 0x0560, "Benq ED8", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x17ff, 0x058d, "Benq T31-16", ALC262_FIXUP_BENQ_T31),
	SND_PCI_QUIRK(0x8086, 0x7270, "BayleyBay", ALC262_FIXUP_INTEL_BAYLEYBAY),
	{}
};

static const struct hda_model_fixup alc262_fixup_models[] = {
	{.id = ALC262_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC262_FIXUP_FSC_H270, .name = "fsc-h270"},
	{.id = ALC262_FIXUP_FSC_S7110, .name = "fsc-s7110"},
	{.id = ALC262_FIXUP_HP_Z200, .name = "hp-z200"},
	{.id = ALC262_FIXUP_TYAN, .name = "tyan"},
	{.id = ALC262_FIXUP_LENOVO_3000, .name = "lenovo-3000"},
	{.id = ALC262_FIXUP_BENQ, .name = "benq"},
	{.id = ALC262_FIXUP_BENQ_T31, .name = "benq-t31"},
	{.id = ALC262_FIXUP_INTEL_BAYLEYBAY, .name = "bayleybay"},
	{}
};

/*
 */
static int patch_alc262(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.shared_mic_vref_pin = 0x18;

	spec->shutup = alc_eapd_shutup;

#if 0
	/* pshou 07/11/05  set a zero PCM sample to DAC when FIFO is
	 * under-run
	 */
	alc_update_coefex_idx(codec, 0x1a, 7, 0, 0x80);
#endif
	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc262_fixup_models, alc262_fixup_tbl,
		       alc262_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	/* automatic parse from the BIOS config */
	err = alc262_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid) {
		err = set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 *  ALC268
 */
/* bind Beep switches of both NID 0x0f and 0x10 */
static int alc268_beep_switch_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned long pval;
	int err;

	mutex_lock(&codec->control_mutex);
	pval = kcontrol->private_value;
	kcontrol->private_value = (pval & ~0xff) | 0x0f;
	err = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	if (err >= 0) {
		kcontrol->private_value = (pval & ~0xff) | 0x10;
		err = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	}
	kcontrol->private_value = pval;
	mutex_unlock(&codec->control_mutex);
	return err;
}

static const struct snd_kcontrol_new alc268_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x1d, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Beep Playback Switch",
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_beep_switch_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x0f, 3, 1, HDA_INPUT)
	},
};

/* set PCBEEP vol = 0, mute connections */
static const struct hda_verb alc268_beep_init_verbs[] = {
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ }
};

enum {
	ALC268_FIXUP_INV_DMIC,
	ALC268_FIXUP_HP_EAPD,
	ALC268_FIXUP_SPDIF,
};

static const struct hda_fixup alc268_fixups[] = {
	[ALC268_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC268_FIXUP_HP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x15, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC268_FIXUP_SPDIF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x014b1180 }, /* enable SPDIF out */
			{}
		}
	},
};

static const struct hda_model_fixup alc268_fixup_models[] = {
	{.id = ALC268_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC268_FIXUP_HP_EAPD, .name = "hp-eapd"},
	{.id = ALC268_FIXUP_SPDIF, .name = "spdif"},
	{}
};

static const struct snd_pci_quirk alc268_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0139, "Acer TravelMate 6293", ALC268_FIXUP_SPDIF),
	SND_PCI_QUIRK(0x1025, 0x015b, "Acer AOA 150 (ZG5)", ALC268_FIXUP_INV_DMIC),
	/* below is codec SSID since multiple Toshiba laptops have the
	 * same PCI SSID 1179:ff00
	 */
	SND_PCI_QUIRK(0x1179, 0xff06, "Toshiba P200", ALC268_FIXUP_HP_EAPD),
	{}
};

/*
 * BIOS auto configuration
 */
static int alc268_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc268_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, NULL, alc268_ssids);
}

/*
 */
static int patch_alc268(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int i, err;

	/* ALC268 has no aa-loopback mixer */
	err = alc_alloc_spec(codec, 0);
	if (err < 0)
		return err;

	spec = codec->spec;
	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	spec->shutup = alc_eapd_shutup;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc268_fixup_models, alc268_fixup_tbl, alc268_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc268_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (err > 0 && !spec->gen.no_analog &&
	    spec->gen.autocfg.speaker_pins[0] != 0x1d) {
		for (i = 0; i < ARRAY_SIZE(alc268_beep_mixer); i++) {
			if (!snd_hda_gen_add_kctl(&spec->gen, NULL,
						  &alc268_beep_mixer[i])) {
				err = -ENOMEM;
				goto error;
			}
		}
		snd_hda_add_verbs(codec, alc268_beep_init_verbs);
		if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
			/* override the amp caps for beep generator */
			snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
					  (0x0c << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x0c << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x07 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC269
 */

static const struct hda_pcm_stream alc269_44k_pcm_analog_playback = {
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
};

static const struct hda_pcm_stream alc269_44k_pcm_analog_capture = {
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
};

/* different alc269-variants */
enum {
	ALC269_TYPE_ALC269VA,
	ALC269_TYPE_ALC269VB,
	ALC269_TYPE_ALC269VC,
	ALC269_TYPE_ALC269VD,
	ALC269_TYPE_ALC280,
	ALC269_TYPE_ALC282,
	ALC269_TYPE_ALC283,
	ALC269_TYPE_ALC284,
	ALC269_TYPE_ALC293,
	ALC269_TYPE_ALC286,
	ALC269_TYPE_ALC298,
	ALC269_TYPE_ALC255,
	ALC269_TYPE_ALC256,
	ALC269_TYPE_ALC257,
	ALC269_TYPE_ALC215,
	ALC269_TYPE_ALC225,
	ALC269_TYPE_ALC294,
	ALC269_TYPE_ALC300,
	ALC269_TYPE_ALC623,
	ALC269_TYPE_ALC700,
};

/*
 * BIOS auto configuration
 */
static int alc269_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc269_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc269_ssids[] = { 0, 0x1b, 0x14, 0x21 };
	static const hda_nid_t alc269va_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	struct alc_spec *spec = codec->spec;
	const hda_nid_t *ssids;

	switch (spec->codec_variant) {
	case ALC269_TYPE_ALC269VA:
	case ALC269_TYPE_ALC269VC:
	case ALC269_TYPE_ALC280:
	case ALC269_TYPE_ALC284:
	case ALC269_TYPE_ALC293:
		ssids = alc269va_ssids;
		break;
	case ALC269_TYPE_ALC269VB:
	case ALC269_TYPE_ALC269VD:
	case ALC269_TYPE_ALC282:
	case ALC269_TYPE_ALC283:
	case ALC269_TYPE_ALC286:
	case ALC269_TYPE_ALC298:
	case ALC269_TYPE_ALC255:
	case ALC269_TYPE_ALC256:
	case ALC269_TYPE_ALC257:
	case ALC269_TYPE_ALC215:
	case ALC269_TYPE_ALC225:
	case ALC269_TYPE_ALC294:
	case ALC269_TYPE_ALC300:
	case ALC269_TYPE_ALC623:
	case ALC269_TYPE_ALC700:
		ssids = alc269_ssids;
		break;
	default:
		ssids = alc269_ssids;
		break;
	}

	return alc_parse_auto_config(codec, alc269_ignore, ssids);
}

static const struct hda_jack_keymap alc_headset_btn_keymap[] = {
	{ SND_JACK_BTN_0, KEY_PLAYPAUSE },
	{ SND_JACK_BTN_1, KEY_VOICECOMMAND },
	{ SND_JACK_BTN_2, KEY_VOLUMEUP },
	{ SND_JACK_BTN_3, KEY_VOLUMEDOWN },
	{}
};

static void alc_headset_btn_callback(struct hda_codec *codec,
				     struct hda_jack_callback *jack)
{
	int report = 0;

	if (jack->unsol_res & (7 << 13))
		report |= SND_JACK_BTN_0;

	if (jack->unsol_res  & (1 << 16 | 3 << 8))
		report |= SND_JACK_BTN_1;

	/* Volume up key */
	if (jack->unsol_res & (7 << 23))
		report |= SND_JACK_BTN_2;

	/* Volume down key */
	if (jack->unsol_res & (7 << 10))
		report |= SND_JACK_BTN_3;

	jack->jack->button_state = report;
}

static void alc_disable_headset_jack_key(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->has_hs_key)
		return;

	switch (codec->core.vendor_id) {
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_write_coef_idx(codec, 0x48, 0x0);
		alc_update_coef_idx(codec, 0x49, 0x0045, 0x0);
		alc_update_coef_idx(codec, 0x44, 0x0045 << 8, 0x0);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_write_coef_idx(codec, 0x48, 0x0);
		alc_update_coef_idx(codec, 0x49, 0x0045, 0x0);
		break;
	}
}

static void alc_enable_headset_jack_key(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->has_hs_key)
		return;

	switch (codec->core.vendor_id) {
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_write_coef_idx(codec, 0x48, 0xd011);
		alc_update_coef_idx(codec, 0x49, 0x007f, 0x0045);
		alc_update_coef_idx(codec, 0x44, 0x007f << 8, 0x0045 << 8);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_write_coef_idx(codec, 0x48, 0xd011);
		alc_update_coef_idx(codec, 0x49, 0x007f, 0x0045);
		break;
	}
}

static void alc_fixup_headset_jack(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->has_hs_key = 1;
		snd_hda_jack_detect_enable_callback(codec, 0x55,
						    alc_headset_btn_callback);
		snd_hda_jack_add_kctl(codec, 0x55, "Headset Jack", false,
				      SND_JACK_HEADSET, alc_headset_btn_keymap);
		break;
	case HDA_FIXUP_ACT_INIT:
		alc_enable_headset_jack_key(codec);
		break;
	}
}

static void alc269vb_toggle_power_output(struct hda_codec *codec, int power_up)
{
	alc_update_coef_idx(codec, 0x04, 1 << 11, power_up ? (1 << 11) : 0);
}

static void alc269_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->codec_variant == ALC269_TYPE_ALC269VB)
		alc269vb_toggle_power_output(codec, 0);
	if (spec->codec_variant == ALC269_TYPE_ALC269VB &&
			(alc_get_coef0(codec) & 0x00ff) == 0x018) {
		msleep(150);
	}
	alc_shutup_pins(codec);
}

static const struct coef_fw alc282_coefs[] = {
	WRITE_COEF(0x03, 0x0002), /* Power Down Control */
	UPDATE_COEF(0x05, 0xff3f, 0x0700), /* FIFO and filter clock */
	WRITE_COEF(0x07, 0x0200), /* DMIC control */
	UPDATE_COEF(0x06, 0x00f0, 0), /* Analog clock */
	UPDATE_COEF(0x08, 0xfffc, 0x0c2c), /* JD */
	WRITE_COEF(0x0a, 0xcccc), /* JD offset1 */
	WRITE_COEF(0x0b, 0xcccc), /* JD offset2 */
	WRITE_COEF(0x0e, 0x6e00), /* LDO1/2/3, DAC/ADC */
	UPDATE_COEF(0x0f, 0xf800, 0x1000), /* JD */
	UPDATE_COEF(0x10, 0xfc00, 0x0c00), /* Capless */
	WRITE_COEF(0x6f, 0x0), /* Class D test 4 */
	UPDATE_COEF(0x0c, 0xfe00, 0), /* IO power down directly */
	WRITE_COEF(0x34, 0xa0c0), /* ANC */
	UPDATE_COEF(0x16, 0x0008, 0), /* AGC MUX */
	UPDATE_COEF(0x1d, 0x00e0, 0), /* DAC simple content protection */
	UPDATE_COEF(0x1f, 0x00e0, 0), /* ADC simple content protection */
	WRITE_COEF(0x21, 0x8804), /* DAC ADC Zero Detection */
	WRITE_COEF(0x63, 0x2902), /* PLL */
	WRITE_COEF(0x68, 0xa080), /* capless control 2 */
	WRITE_COEF(0x69, 0x3400), /* capless control 3 */
	WRITE_COEF(0x6a, 0x2f3e), /* capless control 4 */
	WRITE_COEF(0x6b, 0x0), /* capless control 5 */
	UPDATE_COEF(0x6d, 0x0fff, 0x0900), /* class D test 2 */
	WRITE_COEF(0x6e, 0x110a), /* class D test 3 */
	UPDATE_COEF(0x70, 0x00f8, 0x00d8), /* class D test 5 */
	WRITE_COEF(0x71, 0x0014), /* class D test 6 */
	WRITE_COEF(0x72, 0xc2ba), /* classD OCP */
	UPDATE_COEF(0x77, 0x0f80, 0), /* classD pure DC test */
	WRITE_COEF(0x6c, 0xfc06), /* Class D amp control */
	{}
};

static void alc282_restore_default_value(struct hda_codec *codec)
{
	alc_process_coef_fw(codec, alc282_coefs);
}

static void alc282_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;
	int coef78;

	alc282_restore_default_value(codec);

	if (!hp_pin)
		return;
	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	coef78 = alc_read_coef_idx(codec, 0x78);

	/* Index 0x78 Direct Drive HP AMP LPM Control 1 */
	/* Headphone capless set to high power mode */
	alc_write_coef_idx(codec, 0x78, 0x9004);

	if (hp_pin_sense)
		msleep(2);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	if (hp_pin_sense)
		msleep(100);

	/* Headphone capless set to normal mode */
	alc_write_coef_idx(codec, 0x78, coef78);
}

static void alc282_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;
	int coef78;

	if (!hp_pin) {
		alc269_shutup(codec);
		return;
	}

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	coef78 = alc_read_coef_idx(codec, 0x78);
	alc_write_coef_idx(codec, 0x78, 0x9004);

	if (hp_pin_sense)
		msleep(2);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	if (!spec->no_shutup_pins)
		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	if (hp_pin_sense)
		msleep(100);

	alc_auto_setup_eapd(codec, false);
	alc_shutup_pins(codec);
	alc_write_coef_idx(codec, 0x78, coef78);
}

static const struct coef_fw alc283_coefs[] = {
	WRITE_COEF(0x03, 0x0002), /* Power Down Control */
	UPDATE_COEF(0x05, 0xff3f, 0x0700), /* FIFO and filter clock */
	WRITE_COEF(0x07, 0x0200), /* DMIC control */
	UPDATE_COEF(0x06, 0x00f0, 0), /* Analog clock */
	UPDATE_COEF(0x08, 0xfffc, 0x0c2c), /* JD */
	WRITE_COEF(0x0a, 0xcccc), /* JD offset1 */
	WRITE_COEF(0x0b, 0xcccc), /* JD offset2 */
	WRITE_COEF(0x0e, 0x6fc0), /* LDO1/2/3, DAC/ADC */
	UPDATE_COEF(0x0f, 0xf800, 0x1000), /* JD */
	UPDATE_COEF(0x10, 0xfc00, 0x0c00), /* Capless */
	WRITE_COEF(0x3a, 0x0), /* Class D test 4 */
	UPDATE_COEF(0x0c, 0xfe00, 0x0), /* IO power down directly */
	WRITE_COEF(0x22, 0xa0c0), /* ANC */
	UPDATE_COEFEX(0x53, 0x01, 0x000f, 0x0008), /* AGC MUX */
	UPDATE_COEF(0x1d, 0x00e0, 0), /* DAC simple content protection */
	UPDATE_COEF(0x1f, 0x00e0, 0), /* ADC simple content protection */
	WRITE_COEF(0x21, 0x8804), /* DAC ADC Zero Detection */
	WRITE_COEF(0x2e, 0x2902), /* PLL */
	WRITE_COEF(0x33, 0xa080), /* capless control 2 */
	WRITE_COEF(0x34, 0x3400), /* capless control 3 */
	WRITE_COEF(0x35, 0x2f3e), /* capless control 4 */
	WRITE_COEF(0x36, 0x0), /* capless control 5 */
	UPDATE_COEF(0x38, 0x0fff, 0x0900), /* class D test 2 */
	WRITE_COEF(0x39, 0x110a), /* class D test 3 */
	UPDATE_COEF(0x3b, 0x00f8, 0x00d8), /* class D test 5 */
	WRITE_COEF(0x3c, 0x0014), /* class D test 6 */
	WRITE_COEF(0x3d, 0xc2ba), /* classD OCP */
	UPDATE_COEF(0x42, 0x0f80, 0x0), /* classD pure DC test */
	WRITE_COEF(0x49, 0x0), /* test mode */
	UPDATE_COEF(0x40, 0xf800, 0x9800), /* Class D DC enable */
	UPDATE_COEF(0x42, 0xf000, 0x2000), /* DC offset */
	WRITE_COEF(0x37, 0xfc06), /* Class D amp control */
	UPDATE_COEF(0x1b, 0x8000, 0), /* HP JD control */
	{}
};

static void alc283_restore_default_value(struct hda_codec *codec)
{
	alc_process_coef_fw(codec, alc283_coefs);
}

static void alc283_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;

	alc283_restore_default_value(codec);

	if (!hp_pin)
		return;

	msleep(30);
	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	/* Index 0x43 Direct Drive HP AMP LPM Control 1 */
	/* Headphone capless set to high power mode */
	alc_write_coef_idx(codec, 0x43, 0x9004);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	if (hp_pin_sense)
		msleep(85);
	/* Index 0x46 Combo jack auto switch control 2 */
	/* 3k pull low control for Headset jack. */
	alc_update_coef_idx(codec, 0x46, 3 << 12, 0);
	/* Headphone capless set to normal mode */
	alc_write_coef_idx(codec, 0x43, 0x9614);
}

static void alc283_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;

	if (!hp_pin) {
		alc269_shutup(codec);
		return;
	}

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	alc_write_coef_idx(codec, 0x43, 0x9004);

	/*depop hp during suspend*/
	alc_write_coef_idx(codec, 0x06, 0x2100);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(100);

	if (!spec->no_shutup_pins)
		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	alc_update_coef_idx(codec, 0x46, 0, 3 << 12);

	if (hp_pin_sense)
		msleep(100);
	alc_auto_setup_eapd(codec, false);
	alc_shutup_pins(codec);
	alc_write_coef_idx(codec, 0x43, 0x9614);
}

static void alc256_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;

	if (!hp_pin)
		hp_pin = 0x21;

	msleep(30);

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	if (hp_pin_sense)
		msleep(2);

	alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x1); /* Low power */
	if (spec->ultra_low_power) {
		alc_update_coef_idx(codec, 0x03, 1<<1, 1<<1);
		alc_update_coef_idx(codec, 0x08, 3<<2, 3<<2);
		alc_update_coef_idx(codec, 0x08, 7<<4, 0);
		alc_update_coef_idx(codec, 0x3b, 1<<15, 0);
		alc_update_coef_idx(codec, 0x0e, 7<<6, 7<<6);
		msleep(30);
	}

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense || spec->ultra_low_power)
		msleep(85);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	if (hp_pin_sense || spec->ultra_low_power)
		msleep(100);

	alc_update_coef_idx(codec, 0x46, 3 << 12, 0);
	alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x4); /* Hight power */
	alc_update_coefex_idx(codec, 0x53, 0x02, 0x8000, 1 << 15); /* Clear bit */
	alc_update_coefex_idx(codec, 0x53, 0x02, 0x8000, 0 << 15);
	/*
	 * Expose headphone mic (or possibly Line In on some machines) instead
	 * of PC Beep on 1Ah, and disable 1Ah loopback for all outputs. See
	 * Documentation/sound/hd-audio/realtek-pc-beep.rst for details of
	 * this register.
	 */
	alc_write_coef_idx(codec, 0x36, 0x5757);
}

static void alc256_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;

	if (!hp_pin)
		hp_pin = 0x21;

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	if (hp_pin_sense)
		msleep(2);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense || spec->ultra_low_power)
		msleep(85);

	/* 3k pull low control for Headset jack. */
	/* NOTE: call this before clearing the pin, otherwise codec stalls */
	alc_update_coef_idx(codec, 0x46, 0, 3 << 12);

	if (!spec->no_shutup_pins)
		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	if (hp_pin_sense || spec->ultra_low_power)
		msleep(100);

	alc_auto_setup_eapd(codec, false);
	alc_shutup_pins(codec);
	if (spec->ultra_low_power) {
		msleep(50);
		alc_update_coef_idx(codec, 0x03, 1<<1, 0);
		alc_update_coef_idx(codec, 0x08, 7<<4, 7<<4);
		alc_update_coef_idx(codec, 0x08, 3<<2, 0);
		alc_update_coef_idx(codec, 0x3b, 1<<15, 1<<15);
		alc_update_coef_idx(codec, 0x0e, 7<<6, 0);
		msleep(30);
	}
}

static void alc225_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp1_pin_sense, hp2_pin_sense;

	if (!hp_pin)
		hp_pin = 0x21;
	msleep(30);

	hp1_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	hp2_pin_sense = snd_hda_jack_detect(codec, 0x16);

	if (hp1_pin_sense || hp2_pin_sense)
		msleep(2);

	alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x1); /* Low power */
	if (spec->ultra_low_power) {
		alc_update_coef_idx(codec, 0x08, 0x0f << 2, 3<<2);
		alc_update_coef_idx(codec, 0x0e, 7<<6, 7<<6);
		alc_update_coef_idx(codec, 0x33, 1<<11, 0);
		msleep(30);
	}

	if (hp1_pin_sense || spec->ultra_low_power)
		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	if (hp2_pin_sense)
		snd_hda_codec_write(codec, 0x16, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp1_pin_sense || hp2_pin_sense || spec->ultra_low_power)
		msleep(85);

	if (hp1_pin_sense || spec->ultra_low_power)
		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	if (hp2_pin_sense)
		snd_hda_codec_write(codec, 0x16, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	if (hp1_pin_sense || hp2_pin_sense || spec->ultra_low_power)
		msleep(100);

	alc_update_coef_idx(codec, 0x4a, 3 << 10, 0);
	alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x4); /* Hight power */
}

static void alc225_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp1_pin_sense, hp2_pin_sense;

	if (!hp_pin)
		hp_pin = 0x21;

	alc_disable_headset_jack_key(codec);
	/* 3k pull low control for Headset jack. */
	alc_update_coef_idx(codec, 0x4a, 0, 3 << 10);

	hp1_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	hp2_pin_sense = snd_hda_jack_detect(codec, 0x16);

	if (hp1_pin_sense || hp2_pin_sense)
		msleep(2);

	if (hp1_pin_sense || spec->ultra_low_power)
		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	if (hp2_pin_sense)
		snd_hda_codec_write(codec, 0x16, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp1_pin_sense || hp2_pin_sense || spec->ultra_low_power)
		msleep(85);

	if (hp1_pin_sense || spec->ultra_low_power)
		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);
	if (hp2_pin_sense)
		snd_hda_codec_write(codec, 0x16, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	if (hp1_pin_sense || hp2_pin_sense || spec->ultra_low_power)
		msleep(100);

	alc_auto_setup_eapd(codec, false);
	alc_shutup_pins(codec);
	if (spec->ultra_low_power) {
		msleep(50);
		alc_update_coef_idx(codec, 0x08, 0x0f << 2, 0x0c << 2);
		alc_update_coef_idx(codec, 0x0e, 7<<6, 0);
		alc_update_coef_idx(codec, 0x33, 1<<11, 1<<11);
		alc_update_coef_idx(codec, 0x4a, 3<<4, 2<<4);
		msleep(30);
	}

	alc_update_coef_idx(codec, 0x4a, 3 << 10, 0);
	alc_enable_headset_jack_key(codec);
}

static void alc_default_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;

	if (!hp_pin)
		return;

	msleep(30);

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	if (hp_pin_sense)
		msleep(2);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	if (hp_pin_sense)
		msleep(100);
}

static void alc_default_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp_pin_sense;

	if (!hp_pin) {
		alc269_shutup(codec);
		return;
	}

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	if (hp_pin_sense)
		msleep(2);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	if (!spec->no_shutup_pins)
		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	if (hp_pin_sense)
		msleep(100);

	alc_auto_setup_eapd(codec, false);
	alc_shutup_pins(codec);
}

static void alc294_hp_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	int i, val;

	if (!hp_pin)
		return;

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	msleep(100);

	if (!spec->no_shutup_pins)
		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	alc_update_coef_idx(codec, 0x6f, 0x000f, 0);/* Set HP depop to manual mode */
	alc_update_coefex_idx(codec, 0x58, 0x00, 0x8000, 0x8000); /* HP depop procedure start */

	/* Wait for depop procedure finish  */
	val = alc_read_coefex_idx(codec, 0x58, 0x01);
	for (i = 0; i < 20 && val & 0x0080; i++) {
		msleep(50);
		val = alc_read_coefex_idx(codec, 0x58, 0x01);
	}
	/* Set HP depop to auto mode */
	alc_update_coef_idx(codec, 0x6f, 0x000f, 0x000b);
	msleep(50);
}

static void alc294_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	/* required only at boot or S4 resume time */
	if (!spec->done_hp_init ||
	    codec->core.dev.power.power_state.event == PM_EVENT_RESTORE) {
		alc294_hp_init(codec);
		spec->done_hp_init = true;
	}
	alc_default_init(codec);
}

static void alc5505_coef_set(struct hda_codec *codec, unsigned int index_reg,
			     unsigned int val)
{
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_COEF_INDEX, index_reg >> 1);
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_PROC_COEF, val & 0xffff); /* LSB */
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_PROC_COEF, val >> 16); /* MSB */
}

static int alc5505_coef_get(struct hda_codec *codec, unsigned int index_reg)
{
	unsigned int val;

	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_COEF_INDEX, index_reg >> 1);
	val = snd_hda_codec_read(codec, 0x51, 0, AC_VERB_GET_PROC_COEF, 0)
		& 0xffff;
	val |= snd_hda_codec_read(codec, 0x51, 0, AC_VERB_GET_PROC_COEF, 0)
		<< 16;
	return val;
}

static void alc5505_dsp_halt(struct hda_codec *codec)
{
	unsigned int val;

	alc5505_coef_set(codec, 0x3000, 0x000c); /* DSP CPU stop */
	alc5505_coef_set(codec, 0x880c, 0x0008); /* DDR enter self refresh */
	alc5505_coef_set(codec, 0x61c0, 0x11110080); /* Clock control for PLL and CPU */
	alc5505_coef_set(codec, 0x6230, 0xfc0d4011); /* Disable Input OP */
	alc5505_coef_set(codec, 0x61b4, 0x040a2b03); /* Stop PLL2 */
	alc5505_coef_set(codec, 0x61b0, 0x00005b17); /* Stop PLL1 */
	alc5505_coef_set(codec, 0x61b8, 0x04133303); /* Stop PLL3 */
	val = alc5505_coef_get(codec, 0x6220);
	alc5505_coef_set(codec, 0x6220, (val | 0x3000)); /* switch Ringbuffer clock to DBUS clock */
}

static void alc5505_dsp_back_from_halt(struct hda_codec *codec)
{
	alc5505_coef_set(codec, 0x61b8, 0x04133302);
	alc5505_coef_set(codec, 0x61b0, 0x00005b16);
	alc5505_coef_set(codec, 0x61b4, 0x040a2b02);
	alc5505_coef_set(codec, 0x6230, 0xf80d4011);
	alc5505_coef_set(codec, 0x6220, 0x2002010f);
	alc5505_coef_set(codec, 0x880c, 0x00000004);
}

static void alc5505_dsp_init(struct hda_codec *codec)
{
	unsigned int val;

	alc5505_dsp_halt(codec);
	alc5505_dsp_back_from_halt(codec);
	alc5505_coef_set(codec, 0x61b0, 0x5b14); /* PLL1 control */
	alc5505_coef_set(codec, 0x61b0, 0x5b16);
	alc5505_coef_set(codec, 0x61b4, 0x04132b00); /* PLL2 control */
	alc5505_coef_set(codec, 0x61b4, 0x04132b02);
	alc5505_coef_set(codec, 0x61b8, 0x041f3300); /* PLL3 control*/
	alc5505_coef_set(codec, 0x61b8, 0x041f3302);
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_CODEC_RESET, 0); /* Function reset */
	alc5505_coef_set(codec, 0x61b8, 0x041b3302);
	alc5505_coef_set(codec, 0x61b8, 0x04173302);
	alc5505_coef_set(codec, 0x61b8, 0x04163302);
	alc5505_coef_set(codec, 0x8800, 0x348b328b); /* DRAM control */
	alc5505_coef_set(codec, 0x8808, 0x00020022); /* DRAM control */
	alc5505_coef_set(codec, 0x8818, 0x00000400); /* DRAM control */

	val = alc5505_coef_get(codec, 0x6200) >> 16; /* Read revision ID */
	if (val <= 3)
		alc5505_coef_set(codec, 0x6220, 0x2002010f); /* I/O PAD Configuration */
	else
		alc5505_coef_set(codec, 0x6220, 0x6002018f);

	alc5505_coef_set(codec, 0x61ac, 0x055525f0); /**/
	alc5505_coef_set(codec, 0x61c0, 0x12230080); /* Clock control */
	alc5505_coef_set(codec, 0x61b4, 0x040e2b02); /* PLL2 control */
	alc5505_coef_set(codec, 0x61bc, 0x010234f8); /* OSC Control */
	alc5505_coef_set(codec, 0x880c, 0x00000004); /* DRAM Function control */
	alc5505_coef_set(codec, 0x880c, 0x00000003);
	alc5505_coef_set(codec, 0x880c, 0x00000010);

#ifdef HALT_REALTEK_ALC5505
	alc5505_dsp_halt(codec);
#endif
}

#ifdef HALT_REALTEK_ALC5505
#define alc5505_dsp_suspend(codec)	do { } while (0) /* NOP */
#define alc5505_dsp_resume(codec)	do { } while (0) /* NOP */
#else
#define alc5505_dsp_suspend(codec)	alc5505_dsp_halt(codec)
#define alc5505_dsp_resume(codec)	alc5505_dsp_back_from_halt(codec)
#endif

#ifdef CONFIG_PM
static int alc269_suspend(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->has_alc5505_dsp)
		alc5505_dsp_suspend(codec);
	return alc_suspend(codec);
}

static int alc269_resume(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->codec_variant == ALC269_TYPE_ALC269VB)
		alc269vb_toggle_power_output(codec, 0);
	if (spec->codec_variant == ALC269_TYPE_ALC269VB &&
			(alc_get_coef0(codec) & 0x00ff) == 0x018) {
		msleep(150);
	}

	codec->patch_ops.init(codec);

	if (spec->codec_variant == ALC269_TYPE_ALC269VB)
		alc269vb_toggle_power_output(codec, 1);
	if (spec->codec_variant == ALC269_TYPE_ALC269VB &&
			(alc_get_coef0(codec) & 0x00ff) == 0x017) {
		msleep(200);
	}

	snd_hda_regmap_sync(codec);
	hda_call_check_power_status(codec, 0x01);

	/* on some machine, the BIOS will clear the codec gpio data when enter
	 * suspend, and won't restore the data after resume, so we restore it
	 * in the driver.
	 */
	if (spec->gpio_data)
		alc_write_gpio_data(codec);

	if (spec->has_alc5505_dsp)
		alc5505_dsp_resume(codec);

	return 0;
}
#endif /* CONFIG_PM */

static void alc269_fixup_pincfg_no_hp_to_lineout(struct hda_codec *codec,
						 const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;
}

static void alc269_fixup_pincfg_U7x7_headset_mic(struct hda_codec *codec,
						 const struct hda_fixup *fix,
						 int action)
{
	unsigned int cfg_headphone = snd_hda_codec_get_pincfg(codec, 0x21);
	unsigned int cfg_headset_mic = snd_hda_codec_get_pincfg(codec, 0x19);

	if (cfg_headphone && cfg_headset_mic == 0x411111f0)
		snd_hda_codec_set_pincfg(codec, 0x19,
			(cfg_headphone & ~AC_DEFCFG_DEVICE) |
			(AC_JACK_MIC_IN << AC_DEFCFG_DEVICE_SHIFT));
}

static void alc269_fixup_hweq(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_INIT)
		alc_update_coef_idx(codec, 0x1e, 0, 0x80);
}

static void alc269_fixup_headset_mic(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
}

static void alc271_fixup_dmic(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	static const struct hda_verb verbs[] = {
		{0x20, AC_VERB_SET_COEF_INDEX, 0x0d},
		{0x20, AC_VERB_SET_PROC_COEF, 0x4000},
		{}
	};
	unsigned int cfg;

	if (strcmp(codec->core.chip_name, "ALC271X") &&
	    strcmp(codec->core.chip_name, "ALC269VB"))
		return;
	cfg = snd_hda_codec_get_pincfg(codec, 0x12);
	if (get_defcfg_connect(cfg) == AC_JACK_PORT_FIXED)
		snd_hda_sequence_write(codec, verbs);
}

static void alc269_fixup_pcm_44k(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PROBE)
		return;

	/* Due to a hardware problem on Lenovo Ideadpad, we need to
	 * fix the sample rate of analog I/O to 44.1kHz
	 */
	spec->gen.stream_analog_playback = &alc269_44k_pcm_analog_playback;
	spec->gen.stream_analog_capture = &alc269_44k_pcm_analog_capture;
}

static void alc269_fixup_stereo_dmic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	/* The digital-mic unit sends PDM (differential signal) instead of
	 * the standard PCM, thus you can't record a valid mono stream as is.
	 * Below is a workaround specific to ALC269 to control the dmic
	 * signal source as mono.
	 */
	if (action == HDA_FIXUP_ACT_INIT)
		alc_update_coef_idx(codec, 0x07, 0, 0x80);
}

static void alc269_quanta_automute(struct hda_codec *codec)
{
	snd_hda_gen_update_outputs(codec);

	alc_write_coef_idx(codec, 0x0c, 0x680);
	alc_write_coef_idx(codec, 0x0c, 0x480);
}

static void alc269_fixup_quanta_mute(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action != HDA_FIXUP_ACT_PROBE)
		return;
	spec->gen.automute_hook = alc269_quanta_automute;
}

static void alc269_x101_hp_automute_hook(struct hda_codec *codec,
					 struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	int vref;
	msleep(200);
	snd_hda_gen_hp_automute(codec, jack);

	vref = spec->gen.hp_jack_present ? PIN_VREF80 : 0;
	msleep(100);
	snd_hda_codec_write(codec, 0x18, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    vref);
	msleep(500);
	snd_hda_codec_write(codec, 0x18, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    vref);
}

/*
 * Magic sequence to make Huawei Matebook X right speaker working (bko#197801)
 */
struct hda_alc298_mbxinit {
	unsigned char value_0x23;
	unsigned char value_0x25;
};

static void alc298_huawei_mbx_stereo_seq(struct hda_codec *codec,
					 const struct hda_alc298_mbxinit *initval,
					 bool first)
{
	snd_hda_codec_write(codec, 0x06, 0, AC_VERB_SET_DIGI_CONVERT_3, 0x0);
	alc_write_coef_idx(codec, 0x26, 0xb000);

	if (first)
		snd_hda_codec_write(codec, 0x21, 0, AC_VERB_GET_PIN_SENSE, 0x0);

	snd_hda_codec_write(codec, 0x6, 0, AC_VERB_SET_DIGI_CONVERT_3, 0x80);
	alc_write_coef_idx(codec, 0x26, 0xf000);
	alc_write_coef_idx(codec, 0x23, initval->value_0x23);

	if (initval->value_0x23 != 0x1e)
		alc_write_coef_idx(codec, 0x25, initval->value_0x25);

	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 0x26);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_PROC_COEF, 0xb010);
}

static void alc298_fixup_huawei_mbx_stereo(struct hda_codec *codec,
					   const struct hda_fixup *fix,
					   int action)
{
	/* Initialization magic */
	static const struct hda_alc298_mbxinit dac_init[] = {
		{0x0c, 0x00}, {0x0d, 0x00}, {0x0e, 0x00}, {0x0f, 0x00},
		{0x10, 0x00}, {0x1a, 0x40}, {0x1b, 0x82}, {0x1c, 0x00},
		{0x1d, 0x00}, {0x1e, 0x00}, {0x1f, 0x00},
		{0x20, 0xc2}, {0x21, 0xc8}, {0x22, 0x26}, {0x23, 0x24},
		{0x27, 0xff}, {0x28, 0xff}, {0x29, 0xff}, {0x2a, 0x8f},
		{0x2b, 0x02}, {0x2c, 0x48}, {0x2d, 0x34}, {0x2e, 0x00},
		{0x2f, 0x00},
		{0x30, 0x00}, {0x31, 0x00}, {0x32, 0x00}, {0x33, 0x00},
		{0x34, 0x00}, {0x35, 0x01}, {0x36, 0x93}, {0x37, 0x0c},
		{0x38, 0x00}, {0x39, 0x00}, {0x3a, 0xf8}, {0x38, 0x80},
		{}
	};
	const struct hda_alc298_mbxinit *seq;

	if (action != HDA_FIXUP_ACT_INIT)
		return;

	/* Start */
	snd_hda_codec_write(codec, 0x06, 0, AC_VERB_SET_DIGI_CONVERT_3, 0x00);
	snd_hda_codec_write(codec, 0x06, 0, AC_VERB_SET_DIGI_CONVERT_3, 0x80);
	alc_write_coef_idx(codec, 0x26, 0xf000);
	alc_write_coef_idx(codec, 0x22, 0x31);
	alc_write_coef_idx(codec, 0x23, 0x0b);
	alc_write_coef_idx(codec, 0x25, 0x00);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 0x26);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_PROC_COEF, 0xb010);

	for (seq = dac_init; seq->value_0x23; seq++)
		alc298_huawei_mbx_stereo_seq(codec, seq, seq == dac_init);
}

static void alc269_fixup_x101_headset_mic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		spec->gen.hp_automute_hook = alc269_x101_hp_automute_hook;
	}
}


/* update mute-LED according to the speaker mute state via mic VREF pin */
static void alc269_fixup_mic_mute_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct alc_spec *spec = codec->spec;
	unsigned int pinval;

	if (spec->mute_led_polarity)
		enabled = !enabled;
	pinval = snd_hda_codec_get_pin_target(codec, spec->mute_led_nid);
	pinval &= ~AC_PINCTL_VREFEN;
	pinval |= enabled ? AC_PINCTL_VREF_HIZ : AC_PINCTL_VREF_80;
	if (spec->mute_led_nid) {
		/* temporarily power up/down for setting VREF */
		snd_hda_power_up_pm(codec);
		snd_hda_set_pin_ctl_cache(codec, spec->mute_led_nid, pinval);
		snd_hda_power_down_pm(codec);
	}
}

/* Make sure the led works even in runtime suspend */
static unsigned int led_power_filter(struct hda_codec *codec,
						  hda_nid_t nid,
						  unsigned int power_state)
{
	struct alc_spec *spec = codec->spec;

	if (power_state != AC_PWRST_D3 || nid == 0 ||
	    (nid != spec->mute_led_nid && nid != spec->cap_mute_led_nid))
		return power_state;

	/* Set pin ctl again, it might have just been set to 0 */
	snd_hda_set_pin_ctl(codec, nid,
			    snd_hda_codec_get_pin_target(codec, nid));

	return snd_hda_gen_path_power_filter(codec, nid, power_state);
}

static void alc269_fixup_hp_mute_led(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	const struct dmi_device *dev = NULL;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	while ((dev = dmi_find_device(DMI_DEV_TYPE_OEM_STRING, NULL, dev))) {
		int pol, pin;
		if (sscanf(dev->name, "HP_Mute_LED_%d_%x", &pol, &pin) != 2)
			continue;
		if (pin < 0x0a || pin >= 0x10)
			break;
		spec->mute_led_polarity = pol;
		spec->mute_led_nid = pin - 0x0a + 0x18;
		spec->gen.vmaster_mute.hook = alc269_fixup_mic_mute_hook;
		spec->gen.vmaster_mute_enum = 1;
		codec->power_filter = led_power_filter;
		codec_dbg(codec,
			  "Detected mute LED for %x:%d\n", spec->mute_led_nid,
			   spec->mute_led_polarity);
		break;
	}
}

static void alc269_fixup_hp_mute_led_micx(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_nid = pin;
		spec->gen.vmaster_mute.hook = alc269_fixup_mic_mute_hook;
		spec->gen.vmaster_mute_enum = 1;
		codec->power_filter = led_power_filter;
	}
}

static void alc269_fixup_hp_mute_led_mic1(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc269_fixup_hp_mute_led_micx(codec, fix, action, 0x18);
}

static void alc269_fixup_hp_mute_led_mic2(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc269_fixup_hp_mute_led_micx(codec, fix, action, 0x19);
}

static void alc269_fixup_hp_mute_led_mic3(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc269_fixup_hp_mute_led_micx(codec, fix, action, 0x1b);
}

/* update LED status via GPIO */
static void alc_update_gpio_led(struct hda_codec *codec, unsigned int mask,
				bool enabled)
{
	struct alc_spec *spec = codec->spec;

	if (spec->mute_led_polarity)
		enabled = !enabled;
	alc_update_gpio_data(codec, mask, !enabled); /* muted -> LED on */
}

/* turn on/off mute LED via GPIO per vmaster hook */
static void alc_fixup_gpio_mute_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct alc_spec *spec = codec->spec;

	alc_update_gpio_led(codec, spec->gpio_mute_led_mask, enabled);
}

/* turn on/off mic-mute LED via GPIO per capture hook */
static void alc_gpio_micmute_update(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	alc_update_gpio_led(codec, spec->gpio_mic_led_mask,
			    spec->gen.micmute_led.led_value);
}

/* setup mute and mic-mute GPIO bits, add hooks appropriately */
static void alc_fixup_hp_gpio_led(struct hda_codec *codec,
				  int action,
				  unsigned int mute_mask,
				  unsigned int micmute_mask)
{
	struct alc_spec *spec = codec->spec;

	alc_fixup_gpio(codec, action, mute_mask | micmute_mask);

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	if (mute_mask) {
		spec->gpio_mute_led_mask = mute_mask;
		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
	}
	if (micmute_mask) {
		spec->gpio_mic_led_mask = micmute_mask;
		snd_hda_gen_add_micmute_led(codec, alc_gpio_micmute_update);
	}
}

static void alc269_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x08, 0x10);
}

static void alc285_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x04, 0x00);
}

static void alc286_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x02, 0x20);
}

/* turn on/off mic-mute LED per capture hook */
static void alc_cap_micmute_update(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int pinval;

	if (!spec->cap_mute_led_nid)
		return;
	pinval = snd_hda_codec_get_pin_target(codec, spec->cap_mute_led_nid);
	pinval &= ~AC_PINCTL_VREFEN;
	if (spec->gen.micmute_led.led_value)
		pinval |= AC_PINCTL_VREF_80;
	else
		pinval |= AC_PINCTL_VREF_HIZ;
	snd_hda_set_pin_ctl_cache(codec, spec->cap_mute_led_nid, pinval);
}

static void alc269_fixup_hp_gpio_mic1_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	alc_fixup_hp_gpio_led(codec, action, 0x08, 0);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* Like hp_gpio_mic1_led, but also needs GPIO4 low to
		 * enable headphone amp
		 */
		spec->gpio_mask |= 0x10;
		spec->gpio_dir |= 0x10;
		spec->cap_mute_led_nid = 0x18;
		snd_hda_gen_add_micmute_led(codec, alc_cap_micmute_update);
		codec->power_filter = led_power_filter;
	}
}

static void alc280_fixup_hp_gpio4(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	alc_fixup_hp_gpio_led(codec, action, 0x08, 0);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->cap_mute_led_nid = 0x18;
		snd_hda_gen_add_micmute_led(codec, alc_cap_micmute_update);
		codec->power_filter = led_power_filter;
	}
}

/* update mute-LED according to the speaker mute state via COEF bit */
static void alc_fixup_mute_led_coefbit_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct alc_spec *spec = codec->spec;

	if (spec->mute_led_polarity)
		enabled = !enabled;

	/* temporarily power up/down for setting COEF bit */
	enabled ? alc_update_coef_idx(codec, spec->mute_led_coef_idx,
		spec->mute_led_coefbit_mask, spec->mute_led_coefbit_off) :
		  alc_update_coef_idx(codec, spec->mute_led_coef_idx,
		spec->mute_led_coefbit_mask, spec->mute_led_coefbit_on);
}

static void alc285_fixup_hp_mute_led_coefbit(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef_idx = 0x0b;
		spec->mute_led_coefbit_mask = 1<<3;
		spec->mute_led_coefbit_on = 1<<3;
		spec->mute_led_coefbit_off = 0;
		spec->gen.vmaster_mute.hook = alc_fixup_mute_led_coefbit_hook;
		spec->gen.vmaster_mute_enum = 1;
	}
}

static void alc236_fixup_hp_mute_led_coefbit(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef_idx = 0x34;
		spec->mute_led_coefbit_mask = 1<<5;
		spec->mute_led_coefbit_on = 0;
		spec->mute_led_coefbit_off = 1<<5;
		spec->gen.vmaster_mute.hook = alc_fixup_mute_led_coefbit_hook;
		spec->gen.vmaster_mute_enum = 1;
	}
}

/* turn on/off mic-mute LED per capture hook by coef bit */
static void alc_hp_cap_micmute_update(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->gen.micmute_led.led_value)
		alc_update_coef_idx(codec, spec->mic_led_coef_idx,
			spec->mic_led_coefbit_mask, spec->mic_led_coefbit_on);
	else
		alc_update_coef_idx(codec, spec->mic_led_coef_idx,
			spec->mic_led_coefbit_mask, spec->mic_led_coefbit_off);
}

static void alc285_fixup_hp_coef_micmute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mic_led_coef_idx = 0x19;
		spec->mic_led_coefbit_mask = 1<<13;
		spec->mic_led_coefbit_on = 1<<13;
		spec->mic_led_coefbit_off = 0;
		snd_hda_gen_add_micmute_led(codec, alc_hp_cap_micmute_update);
	}
}

static void alc236_fixup_hp_coef_micmute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mic_led_coef_idx = 0x35;
		spec->mic_led_coefbit_mask = 3<<2;
		spec->mic_led_coefbit_on = 2<<2;
		spec->mic_led_coefbit_off = 1<<2;
		snd_hda_gen_add_micmute_led(codec, alc_hp_cap_micmute_update);
	}
}

static void alc285_fixup_hp_mute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc285_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc285_fixup_hp_coef_micmute_led(codec, fix, action);
}

static void alc236_fixup_hp_mute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc236_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc236_fixup_hp_coef_micmute_led(codec, fix, action);
}

#if IS_REACHABLE(CONFIG_INPUT)
static void gpio2_mic_hotkey_event(struct hda_codec *codec,
				   struct hda_jack_callback *event)
{
	struct alc_spec *spec = codec->spec;

	/* GPIO2 just toggles on a keypress/keyrelease cycle. Therefore
	   send both key on and key off event for every interrupt. */
	input_report_key(spec->kb_dev, spec->alc_mute_keycode_map[ALC_KEY_MICMUTE_INDEX], 1);
	input_sync(spec->kb_dev);
	input_report_key(spec->kb_dev, spec->alc_mute_keycode_map[ALC_KEY_MICMUTE_INDEX], 0);
	input_sync(spec->kb_dev);
}

static int alc_register_micmute_input_device(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	spec->kb_dev = input_allocate_device();
	if (!spec->kb_dev) {
		codec_err(codec, "Out of memory (input_allocate_device)\n");
		return -ENOMEM;
	}

	spec->alc_mute_keycode_map[ALC_KEY_MICMUTE_INDEX] = KEY_MICMUTE;

	spec->kb_dev->name = "Microphone Mute Button";
	spec->kb_dev->evbit[0] = BIT_MASK(EV_KEY);
	spec->kb_dev->keycodesize = sizeof(spec->alc_mute_keycode_map[0]);
	spec->kb_dev->keycodemax = ARRAY_SIZE(spec->alc_mute_keycode_map);
	spec->kb_dev->keycode = spec->alc_mute_keycode_map;
	for (i = 0; i < ARRAY_SIZE(spec->alc_mute_keycode_map); i++)
		set_bit(spec->alc_mute_keycode_map[i], spec->kb_dev->keybit);

	if (input_register_device(spec->kb_dev)) {
		codec_err(codec, "input_register_device failed\n");
		input_free_device(spec->kb_dev);
		spec->kb_dev = NULL;
		return -ENOMEM;
	}

	return 0;
}

/* GPIO1 = set according to SKU external amp
 * GPIO2 = mic mute hotkey
 * GPIO3 = mute LED
 * GPIO4 = mic mute LED
 */
static void alc280_fixup_hp_gpio2_mic_hotkey(struct hda_codec *codec,
					     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	alc_fixup_hp_gpio_led(codec, action, 0x08, 0x10);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->init_amp = ALC_INIT_DEFAULT;
		if (alc_register_micmute_input_device(codec) != 0)
			return;

		spec->gpio_mask |= 0x06;
		spec->gpio_dir |= 0x02;
		spec->gpio_data |= 0x02;
		snd_hda_codec_write_cache(codec, codec->core.afg, 0,
					  AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x04);
		snd_hda_jack_detect_enable_callback(codec, codec->core.afg,
						    gpio2_mic_hotkey_event);
		return;
	}

	if (!spec->kb_dev)
		return;

	switch (action) {
	case HDA_FIXUP_ACT_FREE:
		input_unregister_device(spec->kb_dev);
		spec->kb_dev = NULL;
	}
}

/* Line2 = mic mute hotkey
 * GPIO2 = mic mute LED
 */
static void alc233_fixup_lenovo_line2_mic_hotkey(struct hda_codec *codec,
					     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	alc_fixup_hp_gpio_led(codec, action, 0, 0x04);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->init_amp = ALC_INIT_DEFAULT;
		if (alc_register_micmute_input_device(codec) != 0)
			return;

		snd_hda_jack_detect_enable_callback(codec, 0x1b,
						    gpio2_mic_hotkey_event);
		return;
	}

	if (!spec->kb_dev)
		return;

	switch (action) {
	case HDA_FIXUP_ACT_FREE:
		input_unregister_device(spec->kb_dev);
		spec->kb_dev = NULL;
	}
}
#else /* INPUT */
#define alc280_fixup_hp_gpio2_mic_hotkey	NULL
#define alc233_fixup_lenovo_line2_mic_hotkey	NULL
#endif /* INPUT */

static void alc269_fixup_hp_line1_mic1_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	alc269_fixup_hp_mute_led_micx(codec, fix, action, 0x1a);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->cap_mute_led_nid = 0x18;
		snd_hda_gen_add_micmute_led(codec, alc_cap_micmute_update);
	}
}

static const struct coef_fw alc225_pre_hsmode[] = {
	UPDATE_COEF(0x4a, 1<<8, 0),
	UPDATE_COEFEX(0x57, 0x05, 1<<14, 0),
	UPDATE_COEF(0x63, 3<<14, 3<<14),
	UPDATE_COEF(0x4a, 3<<4, 2<<4),
	UPDATE_COEF(0x4a, 3<<10, 3<<10),
	UPDATE_COEF(0x45, 0x3f<<10, 0x34<<10),
	UPDATE_COEF(0x4a, 3<<10, 0),
	{}
};

static void alc_headset_mode_unplugged(struct hda_codec *codec)
{
	static const struct coef_fw coef0255[] = {
		WRITE_COEF(0x1b, 0x0c0b), /* LDO and MISC control */
		WRITE_COEF(0x45, 0xd089), /* UAJ function set to menual mode */
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 0), /* Direct Drive HP Amp control(Set to verb control)*/
		WRITE_COEF(0x06, 0x6104), /* Set MIC2 Vref gate with HP */
		WRITE_COEFEX(0x57, 0x03, 0x8aa6), /* Direct Drive HP Amp control */
		{}
	};
	static const struct coef_fw coef0256[] = {
		WRITE_COEF(0x1b, 0x0c4b), /* LDO and MISC control */
		WRITE_COEF(0x45, 0xd089), /* UAJ function set to menual mode */
		WRITE_COEF(0x06, 0x6104), /* Set MIC2 Vref gate with HP */
		WRITE_COEFEX(0x57, 0x03, 0x09a3), /* Direct Drive HP Amp control */
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 0), /* Direct Drive HP Amp control(Set to verb control)*/
		{}
	};
	static const struct coef_fw coef0233[] = {
		WRITE_COEF(0x1b, 0x0c0b),
		WRITE_COEF(0x45, 0xc429),
		UPDATE_COEF(0x35, 0x4000, 0),
		WRITE_COEF(0x06, 0x2104),
		WRITE_COEF(0x1a, 0x0001),
		WRITE_COEF(0x26, 0x0004),
		WRITE_COEF(0x32, 0x42a3),
		{}
	};
	static const struct coef_fw coef0288[] = {
		UPDATE_COEF(0x4f, 0xfcc0, 0xc400),
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static const struct coef_fw coef0298[] = {
		UPDATE_COEF(0x19, 0x1300, 0x0300),
		{}
	};
	static const struct coef_fw coef0292[] = {
		WRITE_COEF(0x76, 0x000e),
		WRITE_COEF(0x6c, 0x2400),
		WRITE_COEF(0x18, 0x7308),
		WRITE_COEF(0x6b, 0xc429),
		{}
	};
	static const struct coef_fw coef0293[] = {
		UPDATE_COEF(0x10, 7<<8, 6<<8), /* SET Line1 JD to 0 */
		UPDATE_COEFEX(0x57, 0x05, 1<<15|1<<13, 0x0), /* SET charge pump by verb */
		UPDATE_COEFEX(0x57, 0x03, 1<<10, 1<<10), /* SET EN_OSW to 1 */
		UPDATE_COEF(0x1a, 1<<3, 1<<3), /* Combo JD gating with LINE1-VREFO */
		WRITE_COEF(0x45, 0xc429), /* Set to TRS type */
		UPDATE_COEF(0x4a, 0x000f, 0x000e), /* Combo Jack auto detect */
		{}
	};
	static const struct coef_fw coef0668[] = {
		WRITE_COEF(0x15, 0x0d40),
		WRITE_COEF(0xb7, 0x802b),
		{}
	};
	static const struct coef_fw coef0225[] = {
		UPDATE_COEF(0x63, 3<<14, 0),
		{}
	};
	static const struct coef_fw coef0274[] = {
		UPDATE_COEF(0x4a, 0x0100, 0),
		UPDATE_COEFEX(0x57, 0x05, 0x4000, 0),
		UPDATE_COEF(0x6b, 0xf000, 0x5000),
		UPDATE_COEF(0x4a, 0x0010, 0),
		UPDATE_COEF(0x4a, 0x0c00, 0x0c00),
		WRITE_COEF(0x45, 0x5289),
		UPDATE_COEF(0x4a, 0x0c00, 0),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0256);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		alc_process_coef_fw(codec, coef0274);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0298:
		alc_process_coef_fw(codec, coef0298);
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0668);
		break;
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_process_coef_fw(codec, alc225_pre_hsmode);
		alc_process_coef_fw(codec, coef0225);
		break;
	case 0x10ec0867:
		alc_update_coefex_idx(codec, 0x57, 0x5, 1<<14, 0);
		break;
	}
	codec_dbg(codec, "Headset jack set to unplugged mode.\n");
}


static void alc_headset_mode_mic_in(struct hda_codec *codec, hda_nid_t hp_pin,
				    hda_nid_t mic_pin)
{
	static const struct coef_fw coef0255[] = {
		WRITE_COEFEX(0x57, 0x03, 0x8aa6),
		WRITE_COEF(0x06, 0x6100), /* Set MIC2 Vref gate to normal */
		{}
	};
	static const struct coef_fw coef0256[] = {
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 1<<14), /* Direct Drive HP Amp control(Set to verb control)*/
		WRITE_COEFEX(0x57, 0x03, 0x09a3),
		WRITE_COEF(0x06, 0x6100), /* Set MIC2 Vref gate to normal */
		{}
	};
	static const struct coef_fw coef0233[] = {
		UPDATE_COEF(0x35, 0, 1<<14),
		WRITE_COEF(0x06, 0x2100),
		WRITE_COEF(0x1a, 0x0021),
		WRITE_COEF(0x26, 0x008c),
		{}
	};
	static const struct coef_fw coef0288[] = {
		UPDATE_COEF(0x4f, 0x00c0, 0),
		UPDATE_COEF(0x50, 0x2000, 0),
		UPDATE_COEF(0x56, 0x0006, 0),
		UPDATE_COEF(0x4f, 0xfcc0, 0xc400),
		UPDATE_COEF(0x66, 0x0008, 0x0008),
		UPDATE_COEF(0x67, 0x2000, 0x2000),
		{}
	};
	static const struct coef_fw coef0292[] = {
		WRITE_COEF(0x19, 0xa208),
		WRITE_COEF(0x2e, 0xacf0),
		{}
	};
	static const struct coef_fw coef0293[] = {
		UPDATE_COEFEX(0x57, 0x05, 0, 1<<15|1<<13), /* SET charge pump by verb */
		UPDATE_COEFEX(0x57, 0x03, 1<<10, 0), /* SET EN_OSW to 0 */
		UPDATE_COEF(0x1a, 1<<3, 0), /* Combo JD gating without LINE1-VREFO */
		{}
	};
	static const struct coef_fw coef0688[] = {
		WRITE_COEF(0xb7, 0x802b),
		WRITE_COEF(0xb5, 0x1040),
		UPDATE_COEF(0xc3, 0, 1<<12),
		{}
	};
	static const struct coef_fw coef0225[] = {
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 1<<14),
		UPDATE_COEF(0x4a, 3<<4, 2<<4),
		UPDATE_COEF(0x63, 3<<14, 0),
		{}
	};
	static const struct coef_fw coef0274[] = {
		UPDATE_COEFEX(0x57, 0x05, 0x4000, 0x4000),
		UPDATE_COEF(0x4a, 0x0010, 0),
		UPDATE_COEF(0x6b, 0xf000, 0),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_write_coef_idx(codec, 0x45, 0xc489);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0255);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_write_coef_idx(codec, 0x45, 0xc489);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0256);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		alc_write_coef_idx(codec, 0x45, 0x4689);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0274);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_write_coef_idx(codec, 0x45, 0xc429);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0233);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0298:
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0288);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0292:
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		/* Set to TRS mode */
		alc_write_coef_idx(codec, 0x45, 0xc429);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0293);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0867:
		alc_update_coefex_idx(codec, 0x57, 0x5, 0, 1<<14);
		/* fallthru */
	case 0x10ec0221:
	case 0x10ec0662:
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0668:
		alc_write_coef_idx(codec, 0x11, 0x0001);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0688);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_process_coef_fw(codec, alc225_pre_hsmode);
		alc_update_coef_idx(codec, 0x45, 0x3f<<10, 0x31<<10);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0225);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	}
	codec_dbg(codec, "Headset jack set to mic-in mode.\n");
}

static void alc_headset_mode_default(struct hda_codec *codec)
{
	static const struct coef_fw coef0225[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x30<<10),
		UPDATE_COEF(0x45, 0x3f<<10, 0x31<<10),
		UPDATE_COEF(0x49, 3<<8, 0<<8),
		UPDATE_COEF(0x4a, 3<<4, 3<<4),
		UPDATE_COEF(0x63, 3<<14, 0),
		UPDATE_COEF(0x67, 0xf000, 0x3000),
		{}
	};
	static const struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xc089),
		WRITE_COEF(0x45, 0xc489),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		WRITE_COEF(0x49, 0x0049),
		{}
	};
	static const struct coef_fw coef0256[] = {
		WRITE_COEF(0x45, 0xc489),
		WRITE_COEFEX(0x57, 0x03, 0x0da3),
		WRITE_COEF(0x49, 0x0049),
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 0), /* Direct Drive HP Amp control(Set to verb control)*/
		WRITE_COEF(0x06, 0x6100),
		{}
	};
	static const struct coef_fw coef0233[] = {
		WRITE_COEF(0x06, 0x2100),
		WRITE_COEF(0x32, 0x4ea3),
		{}
	};
	static const struct coef_fw coef0288[] = {
		UPDATE_COEF(0x4f, 0xfcc0, 0xc400), /* Set to TRS type */
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static const struct coef_fw coef0292[] = {
		WRITE_COEF(0x76, 0x000e),
		WRITE_COEF(0x6c, 0x2400),
		WRITE_COEF(0x6b, 0xc429),
		WRITE_COEF(0x18, 0x7308),
		{}
	};
	static const struct coef_fw coef0293[] = {
		UPDATE_COEF(0x4a, 0x000f, 0x000e), /* Combo Jack auto detect */
		WRITE_COEF(0x45, 0xC429), /* Set to TRS type */
		UPDATE_COEF(0x1a, 1<<3, 0), /* Combo JD gating without LINE1-VREFO */
		{}
	};
	static const struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0041),
		WRITE_COEF(0x15, 0x0d40),
		WRITE_COEF(0xb7, 0x802b),
		{}
	};
	static const struct coef_fw coef0274[] = {
		WRITE_COEF(0x45, 0x4289),
		UPDATE_COEF(0x4a, 0x0010, 0x0010),
		UPDATE_COEF(0x6b, 0x0f00, 0),
		UPDATE_COEF(0x49, 0x0300, 0x0300),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_process_coef_fw(codec, alc225_pre_hsmode);
		alc_process_coef_fw(codec, coef0225);
		break;
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_write_coef_idx(codec, 0x1b, 0x0e4b);
		alc_write_coef_idx(codec, 0x45, 0xc089);
		msleep(50);
		alc_process_coef_fw(codec, coef0256);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		alc_process_coef_fw(codec, coef0274);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0298:
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		break;
	case 0x10ec0867:
		alc_update_coefex_idx(codec, 0x57, 0x5, 1<<14, 0);
		break;
	}
	codec_dbg(codec, "Headset jack set to headphone (default) mode.\n");
}

/* Iphone type */
static void alc_headset_mode_ctia(struct hda_codec *codec)
{
	int val;

	static const struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xd489), /* Set to CTIA type */
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		{}
	};
	static const struct coef_fw coef0256[] = {
		WRITE_COEF(0x45, 0xd489), /* Set to CTIA type */
		WRITE_COEF(0x1b, 0x0e6b),
		{}
	};
	static const struct coef_fw coef0233[] = {
		WRITE_COEF(0x45, 0xd429),
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEF(0x32, 0x4ea3),
		{}
	};
	static const struct coef_fw coef0288[] = {
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static const struct coef_fw coef0292[] = {
		WRITE_COEF(0x6b, 0xd429),
		WRITE_COEF(0x76, 0x0008),
		WRITE_COEF(0x18, 0x7388),
		{}
	};
	static const struct coef_fw coef0293[] = {
		WRITE_COEF(0x45, 0xd429), /* Set to ctia type */
		UPDATE_COEF(0x10, 7<<8, 7<<8), /* SET Line1 JD to 1 */
		{}
	};
	static const struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0001),
		WRITE_COEF(0x15, 0x0d60),
		WRITE_COEF(0xc3, 0x0000),
		{}
	};
	static const struct coef_fw coef0225_1[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x35<<10),
		UPDATE_COEF(0x63, 3<<14, 2<<14),
		{}
	};
	static const struct coef_fw coef0225_2[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x35<<10),
		UPDATE_COEF(0x63, 3<<14, 1<<14),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0256);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		alc_write_coef_idx(codec, 0x45, 0xd689);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0298:
		val = alc_read_coef_idx(codec, 0x50);
		if (val & (1 << 12)) {
			alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0020);
			alc_update_coef_idx(codec, 0x4f, 0xfcc0, 0xd400);
			msleep(300);
		} else {
			alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0010);
			alc_update_coef_idx(codec, 0x4f, 0xfcc0, 0xd400);
			msleep(300);
		}
		break;
	case 0x10ec0286:
	case 0x10ec0288:
		alc_update_coef_idx(codec, 0x4f, 0xfcc0, 0xd400);
		msleep(300);
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		break;
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		val = alc_read_coef_idx(codec, 0x45);
		if (val & (1 << 9))
			alc_process_coef_fw(codec, coef0225_2);
		else
			alc_process_coef_fw(codec, coef0225_1);
		break;
	case 0x10ec0867:
		alc_update_coefex_idx(codec, 0x57, 0x5, 1<<14, 0);
		break;
	}
	codec_dbg(codec, "Headset jack set to iPhone-style headset mode.\n");
}

/* Nokia type */
static void alc_headset_mode_omtp(struct hda_codec *codec)
{
	static const struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xe489), /* Set to OMTP Type */
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		{}
	};
	static const struct coef_fw coef0256[] = {
		WRITE_COEF(0x45, 0xe489), /* Set to OMTP Type */
		WRITE_COEF(0x1b, 0x0e6b),
		{}
	};
	static const struct coef_fw coef0233[] = {
		WRITE_COEF(0x45, 0xe429),
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEF(0x32, 0x4ea3),
		{}
	};
	static const struct coef_fw coef0288[] = {
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static const struct coef_fw coef0292[] = {
		WRITE_COEF(0x6b, 0xe429),
		WRITE_COEF(0x76, 0x0008),
		WRITE_COEF(0x18, 0x7388),
		{}
	};
	static const struct coef_fw coef0293[] = {
		WRITE_COEF(0x45, 0xe429), /* Set to omtp type */
		UPDATE_COEF(0x10, 7<<8, 7<<8), /* SET Line1 JD to 1 */
		{}
	};
	static const struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0001),
		WRITE_COEF(0x15, 0x0d50),
		WRITE_COEF(0xc3, 0x0000),
		{}
	};
	static const struct coef_fw coef0225[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x39<<10),
		UPDATE_COEF(0x63, 3<<14, 2<<14),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0256);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		alc_write_coef_idx(codec, 0x45, 0xe689);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0298:
		alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0010);/* Headset output enable */
		alc_update_coef_idx(codec, 0x4f, 0xfcc0, 0xe400);
		msleep(300);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
		alc_update_coef_idx(codec, 0x4f, 0xfcc0, 0xe400);
		msleep(300);
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		break;
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_process_coef_fw(codec, coef0225);
		break;
	}
	codec_dbg(codec, "Headset jack set to Nokia-style headset mode.\n");
}

static void alc_determine_headset_type(struct hda_codec *codec)
{
	int val;
	bool is_ctia = false;
	struct alc_spec *spec = codec->spec;
	static const struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xd089), /* combo jack auto switch control(Check type)*/
		WRITE_COEF(0x49, 0x0149), /* combo jack auto switch control(Vref
 conteol) */
		{}
	};
	static const struct coef_fw coef0288[] = {
		UPDATE_COEF(0x4f, 0xfcc0, 0xd400), /* Check Type */
		{}
	};
	static const struct coef_fw coef0298[] = {
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		UPDATE_COEF(0x19, 0x1300, 0x1300),
		{}
	};
	static const struct coef_fw coef0293[] = {
		UPDATE_COEF(0x4a, 0x000f, 0x0008), /* Combo Jack auto detect */
		WRITE_COEF(0x45, 0xD429), /* Set to ctia type */
		{}
	};
	static const struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0001),
		WRITE_COEF(0xb7, 0x802b),
		WRITE_COEF(0x15, 0x0d60),
		WRITE_COEF(0xc3, 0x0c00),
		{}
	};
	static const struct coef_fw coef0274[] = {
		UPDATE_COEF(0x4a, 0x0010, 0),
		UPDATE_COEF(0x4a, 0x8000, 0),
		WRITE_COEF(0x45, 0xd289),
		UPDATE_COEF(0x49, 0x0300, 0x0300),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_write_coef_idx(codec, 0x1b, 0x0e4b);
		alc_write_coef_idx(codec, 0x06, 0x6104);
		alc_write_coefex_idx(codec, 0x57, 0x3, 0x09a3);

		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
		msleep(80);
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

		alc_process_coef_fw(codec, coef0255);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;

		alc_write_coefex_idx(codec, 0x57, 0x3, 0x0da3);
		alc_update_coefex_idx(codec, 0x57, 0x5, 1<<14, 0);

		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		msleep(80);
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		alc_process_coef_fw(codec, coef0274);
		msleep(80);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x00f0) == 0x00f0;
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_write_coef_idx(codec, 0x45, 0xd029);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0298:
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
		msleep(100);
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);
		msleep(200);

		val = alc_read_coef_idx(codec, 0x50);
		if (val & (1 << 12)) {
			alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0020);
			alc_process_coef_fw(codec, coef0288);
			msleep(350);
			val = alc_read_coef_idx(codec, 0x50);
			is_ctia = (val & 0x0070) == 0x0070;
		} else {
			alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0010);
			alc_process_coef_fw(codec, coef0288);
			msleep(350);
			val = alc_read_coef_idx(codec, 0x50);
			is_ctia = (val & 0x0070) == 0x0070;
		}
		alc_process_coef_fw(codec, coef0298);
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP);
		msleep(75);
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
		alc_process_coef_fw(codec, coef0288);
		msleep(350);
		val = alc_read_coef_idx(codec, 0x50);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0292:
		alc_write_coef_idx(codec, 0x6b, 0xd429);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x6c);
		is_ctia = (val & 0x001c) == 0x001c;
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		msleep(300);
		val = alc_read_coef_idx(codec, 0xbe);
		is_ctia = (val & 0x1c02) == 0x1c02;
		break;
	case 0x10ec0215:
	case 0x10ec0225:
	case 0x10ec0285:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
		msleep(80);
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

		alc_process_coef_fw(codec, alc225_pre_hsmode);
		alc_update_coef_idx(codec, 0x67, 0xf000, 0x1000);
		val = alc_read_coef_idx(codec, 0x45);
		if (val & (1 << 9)) {
			alc_update_coef_idx(codec, 0x45, 0x3f<<10, 0x34<<10);
			alc_update_coef_idx(codec, 0x49, 3<<8, 2<<8);
			msleep(800);
			val = alc_read_coef_idx(codec, 0x46);
			is_ctia = (val & 0x00f0) == 0x00f0;
		} else {
			alc_update_coef_idx(codec, 0x45, 0x3f<<10, 0x34<<10);
			alc_update_coef_idx(codec, 0x49, 3<<8, 1<<8);
			msleep(800);
			val = alc_read_coef_idx(codec, 0x46);
			is_ctia = (val & 0x00f0) == 0x00f0;
		}
		alc_update_coef_idx(codec, 0x4a, 7<<6, 7<<6);
		alc_update_coef_idx(codec, 0x4a, 3<<4, 3<<4);
		alc_update_coef_idx(codec, 0x67, 0xf000, 0x3000);

		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		msleep(80);
		snd_hda_codec_write(codec, 0x21, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
		break;
	case 0x10ec0867:
		is_ctia = true;
		break;
	}

	codec_dbg(codec, "Headset jack detected iPhone-style headset: %s\n",
		    is_ctia ? "yes" : "no");
	spec->current_headset_type = is_ctia ? ALC_HEADSET_TYPE_CTIA : ALC_HEADSET_TYPE_OMTP;
}

static void alc_update_headset_mode(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	hda_nid_t mux_pin = spec->gen.imux_pins[spec->gen.cur_mux[0]];
	hda_nid_t hp_pin = alc_get_hp_pin(spec);

	int new_headset_mode;

	if (!snd_hda_jack_detect(codec, hp_pin))
		new_headset_mode = ALC_HEADSET_MODE_UNPLUGGED;
	else if (mux_pin == spec->headset_mic_pin)
		new_headset_mode = ALC_HEADSET_MODE_HEADSET;
	else if (mux_pin == spec->headphone_mic_pin)
		new_headset_mode = ALC_HEADSET_MODE_MIC;
	else
		new_headset_mode = ALC_HEADSET_MODE_HEADPHONE;

	if (new_headset_mode == spec->current_headset_mode) {
		snd_hda_gen_update_outputs(codec);
		return;
	}

	switch (new_headset_mode) {
	case ALC_HEADSET_MODE_UNPLUGGED:
		alc_headset_mode_unplugged(codec);
		spec->current_headset_mode = ALC_HEADSET_MODE_UNKNOWN;
		spec->current_headset_type = ALC_HEADSET_TYPE_UNKNOWN;
		spec->gen.hp_jack_present = false;
		break;
	case ALC_HEADSET_MODE_HEADSET:
		if (spec->current_headset_type == ALC_HEADSET_TYPE_UNKNOWN)
			alc_determine_headset_type(codec);
		if (spec->current_headset_type == ALC_HEADSET_TYPE_CTIA)
			alc_headset_mode_ctia(codec);
		else if (spec->current_headset_type == ALC_HEADSET_TYPE_OMTP)
			alc_headset_mode_omtp(codec);
		spec->gen.hp_jack_present = true;
		break;
	case ALC_HEADSET_MODE_MIC:
		alc_headset_mode_mic_in(codec, hp_pin, spec->headphone_mic_pin);
		spec->gen.hp_jack_present = false;
		break;
	case ALC_HEADSET_MODE_HEADPHONE:
		alc_headset_mode_default(codec);
		spec->gen.hp_jack_present = true;
		break;
	}
	if (new_headset_mode != ALC_HEADSET_MODE_MIC) {
		snd_hda_set_pin_ctl_cache(codec, hp_pin,
					  AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
		if (spec->headphone_mic_pin && spec->headphone_mic_pin != hp_pin)
			snd_hda_set_pin_ctl_cache(codec, spec->headphone_mic_pin,
						  PIN_VREFHIZ);
	}
	spec->current_headset_mode = new_headset_mode;

	snd_hda_gen_update_outputs(codec);
}

static void alc_update_headset_mode_hook(struct hda_codec *codec,
					 struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	alc_update_headset_mode(codec);
}

static void alc_update_headset_jack_cb(struct hda_codec *codec,
				       struct hda_jack_callback *jack)
{
	snd_hda_gen_hp_automute(codec, jack);
}

static void alc_probe_headset_mode(struct hda_codec *codec)
{
	int i;
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;

	/* Find mic pins */
	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].is_headset_mic && !spec->headset_mic_pin)
			spec->headset_mic_pin = cfg->inputs[i].pin;
		if (cfg->inputs[i].is_headphone_mic && !spec->headphone_mic_pin)
			spec->headphone_mic_pin = cfg->inputs[i].pin;
	}

	WARN_ON(spec->gen.cap_sync_hook);
	spec->gen.cap_sync_hook = alc_update_headset_mode_hook;
	spec->gen.automute_hook = alc_update_headset_mode;
	spec->gen.hp_automute_hook = alc_update_headset_jack_cb;
}

static void alc_fixup_headset_mode(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC | HDA_PINCFG_HEADPHONE_MIC;
		break;
	case HDA_FIXUP_ACT_PROBE:
		alc_probe_headset_mode(codec);
		break;
	case HDA_FIXUP_ACT_INIT:
		if (is_s3_resume(codec) || is_s4_resume(codec)) {
			spec->current_headset_mode = ALC_HEADSET_MODE_UNKNOWN;
			spec->current_headset_type = ALC_HEADSET_TYPE_UNKNOWN;
		}
		alc_update_headset_mode(codec);
		break;
	}
}

static void alc_fixup_headset_mode_no_hp_mic(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
	}
	else
		alc_fixup_headset_mode(codec, fix, action);
}

static void alc255_set_default_jack_type(struct hda_codec *codec)
{
	/* Set to iphone type */
	static const struct coef_fw alc255fw[] = {
		WRITE_COEF(0x1b, 0x880b),
		WRITE_COEF(0x45, 0xd089),
		WRITE_COEF(0x1b, 0x080b),
		WRITE_COEF(0x46, 0x0004),
		WRITE_COEF(0x1b, 0x0c0b),
		{}
	};
	static const struct coef_fw alc256fw[] = {
		WRITE_COEF(0x1b, 0x884b),
		WRITE_COEF(0x45, 0xd089),
		WRITE_COEF(0x1b, 0x084b),
		WRITE_COEF(0x46, 0x0004),
		WRITE_COEF(0x1b, 0x0c4b),
		{}
	};
	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, alc255fw);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, alc256fw);
		break;
	}
	msleep(30);
}

static void alc_fixup_headset_mode_alc255(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		alc255_set_default_jack_type(codec);
	}
	alc_fixup_headset_mode(codec, fix, action);
}

static void alc_fixup_headset_mode_alc255_no_hp_mic(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		alc255_set_default_jack_type(codec);
	} 
	else
		alc_fixup_headset_mode(codec, fix, action);
}

static void alc288_update_headset_jack_cb(struct hda_codec *codec,
				       struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;

	alc_update_headset_jack_cb(codec, jack);
	/* Headset Mic enable or disable, only for Dell Dino */
	alc_update_gpio_data(codec, 0x40, spec->gen.hp_jack_present);
}

static void alc_fixup_headset_mode_dell_alc288(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_headset_mode(codec, fix, action);
	if (action == HDA_FIXUP_ACT_PROBE) {
		struct alc_spec *spec = codec->spec;
		/* toggled via hp_automute_hook */
		spec->gpio_mask |= 0x40;
		spec->gpio_dir |= 0x40;
		spec->gen.hp_automute_hook = alc288_update_headset_jack_cb;
	}
}

static void alc_fixup_auto_mute_via_amp(struct hda_codec *codec,
					const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->gen.auto_mute_via_amp = 1;
	}
}

static void alc_fixup_no_shutup(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->no_shutup_pins = 1;
	}
}

static void alc_fixup_disable_aamix(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		/* Disable AA-loopback as it causes white noise */
		spec->gen.mixer_nid = 0;
	}
}

/* fixup for Thinkpad docks: add dock pins, avoid HP parser fixup */
static void alc_fixup_tpt440_dock(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	static const struct hda_pintbl pincfgs[] = {
		{ 0x16, 0x21211010 }, /* dock headphone */
		{ 0x19, 0x21a11010 }, /* dock mic */
		{ }
	};
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->reboot_notify = snd_hda_gen_reboot_notify; /* reduce noise */
		spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;
		codec->power_save_node = 0; /* avoid click noises */
		snd_hda_apply_pincfgs(codec, pincfgs);
	}
}

static void alc_fixup_tpt470_dock(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	static const struct hda_pintbl pincfgs[] = {
		{ 0x17, 0x21211010 }, /* dock headphone */
		{ 0x19, 0x21a11010 }, /* dock mic */
		{ }
	};
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;
		snd_hda_apply_pincfgs(codec, pincfgs);
	} else if (action == HDA_FIXUP_ACT_INIT) {
		/* Enable DOCK device */
		snd_hda_codec_write(codec, 0x17, 0,
			    AC_VERB_SET_CONFIG_DEFAULT_BYTES_3, 0);
		/* Enable DOCK device */
		snd_hda_codec_write(codec, 0x19, 0,
			    AC_VERB_SET_CONFIG_DEFAULT_BYTES_3, 0);
	}
}

static void alc_fixup_tpt470_dacs(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	/* Assure the speaker pin to be coupled with DAC NID 0x03; otherwise
	 * the speaker output becomes too low by some reason on Thinkpads with
	 * ALC298 codec
	 */
	static const hda_nid_t preferred_pairs[] = {
		0x14, 0x03, 0x17, 0x02, 0x21, 0x02,
		0
	};
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->gen.preferred_dacs = preferred_pairs;
}

static void alc_shutup_dell_xps13(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int hp_pin = alc_get_hp_pin(spec);

	/* Prevent pop noises when headphones are plugged in */
	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	msleep(20);
}

static void alc_fixup_dell_xps13(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->gen.input_mux;
	int i;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* mic pin 0x19 must be initialized with Vref Hi-Z, otherwise
		 * it causes a click noise at start up
		 */
		snd_hda_codec_set_pin_target(codec, 0x19, PIN_VREFHIZ);
		spec->shutup = alc_shutup_dell_xps13;
		break;
	case HDA_FIXUP_ACT_PROBE:
		/* Make the internal mic the default input source. */
		for (i = 0; i < imux->num_items; i++) {
			if (spec->gen.imux_pins[i] == 0x12) {
				spec->gen.cur_mux[0] = i;
				break;
			}
		}
		break;
	}
}

static void alc_fixup_headset_mode_alc662(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		spec->gen.hp_mic = 1; /* Mic-in is same pin as headphone */

		/* Disable boost for mic-in permanently. (This code is only called
		   from quirks that guarantee that the headphone is at NID 0x1b.) */
		snd_hda_codec_write(codec, 0x1b, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000);
		snd_hda_override_wcaps(codec, 0x1b, get_wcaps(codec, 0x1b) & ~AC_WCAP_IN_AMP);
	} else
		alc_fixup_headset_mode(codec, fix, action);
}

static void alc_fixup_headset_mode_alc668(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		alc_write_coef_idx(codec, 0xc4, 0x8000);
		alc_update_coef_idx(codec, 0xc2, ~0xfe, 0);
		snd_hda_set_pin_ctl_cache(codec, 0x18, 0);
	}
	alc_fixup_headset_mode(codec, fix, action);
}

/* Returns the nid of the external mic input pin, or 0 if it cannot be found. */
static int find_ext_mic_pin(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	hda_nid_t nid;
	unsigned int defcfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].type != AUTO_PIN_MIC)
			continue;
		nid = cfg->inputs[i].pin;
		defcfg = snd_hda_codec_get_pincfg(codec, nid);
		if (snd_hda_get_input_pin_attr(defcfg) == INPUT_PIN_ATTR_INT)
			continue;
		return nid;
	}

	return 0;
}

static void alc271_hp_gate_mic_jack(struct hda_codec *codec,
				    const struct hda_fixup *fix,
				    int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		int mic_pin = find_ext_mic_pin(codec);
		int hp_pin = alc_get_hp_pin(spec);

		if (snd_BUG_ON(!mic_pin || !hp_pin))
			return;
		snd_hda_jack_set_gating_jack(codec, mic_pin, hp_pin);
	}
}

static void alc269_fixup_limit_int_mic_boost(struct hda_codec *codec,
					     const struct hda_fixup *fix,
					     int action)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	int i;

	/* The mic boosts on level 2 and 3 are too noisy
	   on the internal mic input.
	   Therefore limit the boost to 0 or 1. */

	if (action != HDA_FIXUP_ACT_PROBE)
		return;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		unsigned int defcfg;
		if (cfg->inputs[i].type != AUTO_PIN_MIC)
			continue;
		defcfg = snd_hda_codec_get_pincfg(codec, nid);
		if (snd_hda_get_input_pin_attr(defcfg) != INPUT_PIN_ATTR_INT)
			continue;

		snd_hda_override_amp_caps(codec, nid, HDA_INPUT,
					  (0x00 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x01 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x2f << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));
	}
}

static void alc283_hp_automute_hook(struct hda_codec *codec,
				    struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	int vref;

	msleep(200);
	snd_hda_gen_hp_automute(codec, jack);

	vref = spec->gen.hp_jack_present ? PIN_VREF80 : 0;

	msleep(600);
	snd_hda_codec_write(codec, 0x19, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    vref);
}

static void alc283_fixup_chromebook(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_override_wcaps(codec, 0x03, 0);
		/* Disable AA-loopback as it causes white noise */
		spec->gen.mixer_nid = 0;
		break;
	case HDA_FIXUP_ACT_INIT:
		/* MIC2-VREF control */
		/* Set to manual mode */
		alc_update_coef_idx(codec, 0x06, 0x000c, 0);
		/* Enable Line1 input control by verb */
		alc_update_coef_idx(codec, 0x1a, 0, 1 << 4);
		break;
	}
}

static void alc283_fixup_sense_combo_jack(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gen.hp_automute_hook = alc283_hp_automute_hook;
		break;
	case HDA_FIXUP_ACT_INIT:
		/* MIC2-VREF control */
		/* Set to manual mode */
		alc_update_coef_idx(codec, 0x06, 0x000c, 0);
		break;
	}
}

/* mute tablet speaker pin (0x14) via dock plugging in addition */
static void asus_tx300_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_gen_update_outputs(codec);
	if (snd_hda_jack_detect(codec, 0x1b))
		spec->gen.mute_bits |= (1ULL << 0x14);
}

static void alc282_fixup_asus_tx300(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_pintbl dock_pins[] = {
		{ 0x1b, 0x21114000 }, /* dock speaker pin */
		{}
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->init_amp = ALC_INIT_DEFAULT;
		/* TX300 needs to set up GPIO2 for the speaker amp */
		alc_setup_gpio(codec, 0x04);
		snd_hda_apply_pincfgs(codec, dock_pins);
		spec->gen.auto_mute_via_amp = 1;
		spec->gen.automute_hook = asus_tx300_automute;
		snd_hda_jack_detect_enable_callback(codec, 0x1b,
						    snd_hda_gen_hp_automute);
		break;
	case HDA_FIXUP_ACT_PROBE:
		spec->init_amp = ALC_INIT_DEFAULT;
		break;
	case HDA_FIXUP_ACT_BUILD:
		/* this is a bit tricky; give more sane names for the main
		 * (tablet) speaker and the dock speaker, respectively
		 */
		rename_ctl(codec, "Speaker Playback Switch",
			   "Dock Speaker Playback Switch");
		rename_ctl(codec, "Bass Speaker Playback Switch",
			   "Speaker Playback Switch");
		break;
	}
}

static void alc290_fixup_mono_speakers(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* DAC node 0x03 is giving mono output. We therefore want to
		   make sure 0x14 (front speaker) and 0x15 (headphones) use the
		   stereo DAC, while leaving 0x17 (bass speaker) for node 0x03. */
		static const hda_nid_t conn1[] = { 0x0c };
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn1), conn1);
		snd_hda_override_conn_list(codec, 0x15, ARRAY_SIZE(conn1), conn1);
	}
}

static void alc298_fixup_speaker_volume(struct hda_codec *codec,
					const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* The speaker is routed to the Node 0x06 by a mistake, as a result
		   we can't adjust the speaker's volume since this node does not has
		   Amp-out capability. we change the speaker's route to:
		   Node 0x02 (Audio Output) -> Node 0x0c (Audio Mixer) -> Node 0x17 (
		   Pin Complex), since Node 0x02 has Amp-out caps, we can adjust
		   speaker's volume now. */

		static const hda_nid_t conn1[] = { 0x0c };
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn1), conn1);
	}
}

/* disable DAC3 (0x06) selection on NID 0x17 as it has no volume amp control */
static void alc295_fixup_disable_dac3(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		static const hda_nid_t conn[] = { 0x02, 0x03 };
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
	}
}

/* force NID 0x17 (Bass Speaker) to DAC1 to share it with the main speaker */
static void alc285_fixup_speaker2_to_dac1(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		static const hda_nid_t conn[] = { 0x02 };
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
	}
}

/* Hook to update amp GPIO4 for automute */
static void alc280_hp_gpio4_automute_hook(struct hda_codec *codec,
					  struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_gen_hp_automute(codec, jack);
	/* mute_led_polarity is set to 0, so we pass inverted value here */
	alc_update_gpio_led(codec, 0x10, !spec->gen.hp_jack_present);
}

/* Manage GPIOs for HP EliteBook Folio 9480m.
 *
 * GPIO4 is the headphone amplifier power control
 * GPIO3 is the audio output mute indicator LED
 */

static void alc280_fixup_hp_9480m(struct hda_codec *codec,
				  const struct hda_fixup *fix,
				  int action)
{
	struct alc_spec *spec = codec->spec;

	alc_fixup_hp_gpio_led(codec, action, 0x08, 0);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* amp at GPIO4; toggled via alc280_hp_gpio4_automute_hook() */
		spec->gpio_mask |= 0x10;
		spec->gpio_dir |= 0x10;
		spec->gen.hp_automute_hook = alc280_hp_gpio4_automute_hook;
	}
}

static void alc275_fixup_gpio4_off(struct hda_codec *codec,
				   const struct hda_fixup *fix,
				   int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gpio_mask |= 0x04;
		spec->gpio_dir |= 0x04;
		/* set data bit low */
	}
}

static void alc233_alc662_fixup_lenovo_dual_codecs(struct hda_codec *codec,
					 const struct hda_fixup *fix,
					 int action)
{
	alc_fixup_dual_codecs(codec, fix, action);
	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* override card longname to provide a unique UCM profile */
		strcpy(codec->card->longname, "HDAudio-Lenovo-DualCodecs");
		break;
	case HDA_FIXUP_ACT_BUILD:
		/* rename Capture controls depending on the codec */
		rename_ctl(codec, "Capture Volume",
			   codec->addr == 0 ?
			   "Rear-Panel Capture Volume" :
			   "Front-Panel Capture Volume");
		rename_ctl(codec, "Capture Switch",
			   codec->addr == 0 ?
			   "Rear-Panel Capture Switch" :
			   "Front-Panel Capture Switch");
		break;
	}
}

static void alc225_fixup_s3_pop_noise(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	codec->power_save_node = 1;
}

/* Forcibly assign NID 0x03 to HP/LO while NID 0x02 to SPK for EQ */
static void alc274_fixup_bind_dacs(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const hda_nid_t preferred_pairs[] = {
		0x21, 0x03, 0x1b, 0x03, 0x16, 0x02,
		0
	};

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	spec->gen.preferred_dacs = preferred_pairs;
	spec->gen.auto_mute_via_amp = 1;
	codec->power_save_node = 0;
}

/* The DAC of NID 0x3 will introduce click/pop noise on headphones, so invalidate it */
static void alc285_fixup_invalidate_dacs(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_override_wcaps(codec, 0x03, 0);
}

static void alc295_fixup_chromebook(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->ultra_low_power = true;
		break;
	case HDA_FIXUP_ACT_INIT:
		switch (codec->core.vendor_id) {
		case 0x10ec0295:
			alc_update_coef_idx(codec, 0x4a, 0x8000, 1 << 15); /* Reset HP JD */
			alc_update_coef_idx(codec, 0x4a, 0x8000, 0 << 15);
			break;
		case 0x10ec0236:
			alc_update_coef_idx(codec, 0x1b, 0x8000, 1 << 15); /* Reset HP JD */
			alc_update_coef_idx(codec, 0x1b, 0x8000, 0 << 15);
			break;
		}
		break;
	}
}

static void alc_fixup_disable_mic_vref(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_codec_set_pin_target(codec, 0x19, PIN_VREFHIZ);
}

/* for hda_fixup_thinkpad_acpi() */
#include "thinkpad_helper.c"

static void alc_fixup_thinkpad_acpi(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	alc_fixup_no_shutup(codec, fix, action); /* reduce click noise */
	hda_fixup_thinkpad_acpi(codec, fix, action);
}

/* for alc295_fixup_hp_top_speakers */
#include "hp_x360_helper.c"

enum {
	ALC269_FIXUP_SONY_VAIO,
	ALC275_FIXUP_SONY_VAIO_GPIO2,
	ALC269_FIXUP_DELL_M101Z,
	ALC269_FIXUP_SKU_IGNORE,
	ALC269_FIXUP_ASUS_G73JW,
	ALC269_FIXUP_LENOVO_EAPD,
	ALC275_FIXUP_SONY_HWEQ,
	ALC275_FIXUP_SONY_DISABLE_AAMIX,
	ALC271_FIXUP_DMIC,
	ALC269_FIXUP_PCM_44K,
	ALC269_FIXUP_STEREO_DMIC,
	ALC269_FIXUP_HEADSET_MIC,
	ALC269_FIXUP_QUANTA_MUTE,
	ALC269_FIXUP_LIFEBOOK,
	ALC269_FIXUP_LIFEBOOK_EXTMIC,
	ALC269_FIXUP_LIFEBOOK_HP_PIN,
	ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT,
	ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC,
	ALC269_FIXUP_AMIC,
	ALC269_FIXUP_DMIC,
	ALC269VB_FIXUP_AMIC,
	ALC269VB_FIXUP_DMIC,
	ALC269_FIXUP_HP_MUTE_LED,
	ALC269_FIXUP_HP_MUTE_LED_MIC1,
	ALC269_FIXUP_HP_MUTE_LED_MIC2,
	ALC269_FIXUP_HP_MUTE_LED_MIC3,
	ALC269_FIXUP_HP_GPIO_LED,
	ALC269_FIXUP_HP_GPIO_MIC1_LED,
	ALC269_FIXUP_HP_LINE1_MIC1_LED,
	ALC269_FIXUP_INV_DMIC,
	ALC269_FIXUP_LENOVO_DOCK,
	ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST,
	ALC269_FIXUP_NO_SHUTUP,
	ALC286_FIXUP_SONY_MIC_NO_PRESENCE,
	ALC269_FIXUP_PINCFG_NO_HP_TO_LINEOUT,
	ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL2_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL3_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
	ALC269_FIXUP_HEADSET_MODE,
	ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC,
	ALC269_FIXUP_ASPIRE_HEADSET_MIC,
	ALC269_FIXUP_ASUS_X101_FUNC,
	ALC269_FIXUP_ASUS_X101_VERB,
	ALC269_FIXUP_ASUS_X101,
	ALC271_FIXUP_AMIC_MIC2,
	ALC271_FIXUP_HP_GATE_MIC_JACK,
	ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572,
	ALC269_FIXUP_ACER_AC700,
	ALC269_FIXUP_LIMIT_INT_MIC_BOOST,
	ALC269VB_FIXUP_ASUS_ZENBOOK,
	ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A,
	ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED,
	ALC269VB_FIXUP_ORDISSIMO_EVE2,
	ALC283_FIXUP_CHROME_BOOK,
	ALC283_FIXUP_SENSE_COMBO_JACK,
	ALC282_FIXUP_ASUS_TX300,
	ALC283_FIXUP_INT_MIC,
	ALC290_FIXUP_MONO_SPEAKERS,
	ALC290_FIXUP_MONO_SPEAKERS_HSJACK,
	ALC290_FIXUP_SUBWOOFER,
	ALC290_FIXUP_SUBWOOFER_HSJACK,
	ALC269_FIXUP_THINKPAD_ACPI,
	ALC269_FIXUP_DMIC_THINKPAD_ACPI,
	ALC255_FIXUP_ACER_MIC_NO_PRESENCE,
	ALC255_FIXUP_ASUS_MIC_NO_PRESENCE,
	ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC255_FIXUP_DELL2_MIC_NO_PRESENCE,
	ALC255_FIXUP_HEADSET_MODE,
	ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC,
	ALC293_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC292_FIXUP_TPT440_DOCK,
	ALC292_FIXUP_TPT440,
	ALC283_FIXUP_HEADSET_MIC,
	ALC255_FIXUP_MIC_MUTE_LED,
	ALC282_FIXUP_ASPIRE_V5_PINS,
	ALC280_FIXUP_HP_GPIO4,
	ALC286_FIXUP_HP_GPIO_LED,
	ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY,
	ALC280_FIXUP_HP_DOCK_PINS,
	ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED,
	ALC280_FIXUP_HP_9480M,
	ALC288_FIXUP_DELL_HEADSET_MODE,
	ALC288_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC288_FIXUP_DELL_XPS_13,
	ALC288_FIXUP_DISABLE_AAMIX,
	ALC292_FIXUP_DELL_E7X_AAMIX,
	ALC292_FIXUP_DELL_E7X,
	ALC292_FIXUP_DISABLE_AAMIX,
	ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK,
	ALC298_FIXUP_ALIENWARE_MIC_NO_PRESENCE,
	ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE,
	ALC275_FIXUP_DELL_XPS,
	ALC293_FIXUP_LENOVO_SPK_NOISE,
	ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY,
	ALC255_FIXUP_DELL_SPK_NOISE,
	ALC225_FIXUP_DISABLE_MIC_VREF,
	ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC295_FIXUP_DISABLE_DAC3,
	ALC285_FIXUP_SPEAKER2_TO_DAC1,
	ALC280_FIXUP_HP_HEADSET_MIC,
	ALC221_FIXUP_HP_FRONT_MIC,
	ALC292_FIXUP_TPT460,
	ALC298_FIXUP_SPK_VOLUME,
	ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER,
	ALC269_FIXUP_ATIV_BOOK_8,
	ALC221_FIXUP_HP_MIC_NO_PRESENCE,
	ALC256_FIXUP_ASUS_HEADSET_MODE,
	ALC256_FIXUP_ASUS_MIC,
	ALC256_FIXUP_ASUS_AIO_GPIO2,
	ALC233_FIXUP_ASUS_MIC_NO_PRESENCE,
	ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE,
	ALC233_FIXUP_LENOVO_MULTI_CODECS,
	ALC233_FIXUP_ACER_HEADSET_MIC,
	ALC294_FIXUP_LENOVO_MIC_LOCATION,
	ALC225_FIXUP_DELL_WYSE_MIC_NO_PRESENCE,
	ALC225_FIXUP_S3_POP_NOISE,
	ALC700_FIXUP_INTEL_REFERENCE,
	ALC274_FIXUP_DELL_BIND_DACS,
	ALC274_FIXUP_DELL_AIO_LINEOUT_VERB,
	ALC298_FIXUP_TPT470_DOCK_FIX,
	ALC298_FIXUP_TPT470_DOCK,
	ALC255_FIXUP_DUMMY_LINEOUT_VERB,
	ALC255_FIXUP_DELL_HEADSET_MIC,
	ALC256_FIXUP_HUAWEI_MACH_WX9_PINS,
	ALC298_FIXUP_HUAWEI_MBX_STEREO,
	ALC295_FIXUP_HP_X360,
	ALC221_FIXUP_HP_HEADSET_MIC,
	ALC285_FIXUP_LENOVO_HEADPHONE_NOISE,
	ALC295_FIXUP_HP_AUTO_MUTE,
	ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE,
	ALC294_FIXUP_ASUS_MIC,
	ALC294_FIXUP_ASUS_HEADSET_MIC,
	ALC294_FIXUP_ASUS_SPK,
	ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE,
	ALC285_FIXUP_LENOVO_PC_BEEP_IN_NOISE,
	ALC255_FIXUP_ACER_HEADSET_MIC,
	ALC295_FIXUP_CHROME_BOOK,
	ALC225_FIXUP_HEADSET_JACK,
	ALC225_FIXUP_DELL_WYSE_AIO_MIC_NO_PRESENCE,
	ALC225_FIXUP_WYSE_AUTO_MUTE,
	ALC225_FIXUP_WYSE_DISABLE_MIC_VREF,
	ALC286_FIXUP_ACER_AIO_HEADSET_MIC,
	ALC256_FIXUP_ASUS_HEADSET_MIC,
	ALC256_FIXUP_ASUS_MIC_NO_PRESENCE,
	ALC299_FIXUP_PREDATOR_SPK,
	ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE,
	ALC289_FIXUP_DELL_SPK2,
	ALC289_FIXUP_DUAL_SPK,
	ALC294_FIXUP_SPK2_TO_DAC1,
	ALC294_FIXUP_ASUS_DUAL_SPK,
	ALC285_FIXUP_THINKPAD_HEADSET_JACK,
	ALC294_FIXUP_ASUS_HPE,
	ALC294_FIXUP_ASUS_COEF_1B,
	ALC285_FIXUP_HP_GPIO_LED,
	ALC285_FIXUP_HP_MUTE_LED,
	ALC236_FIXUP_HP_MUTE_LED,
	ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET,
	ALC295_FIXUP_ASUS_MIC_NO_PRESENCE,
};

static const struct hda_fixup alc269_fixups[] = {
	[ALC269_FIXUP_SONY_VAIO] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x19, PIN_VREFGRD},
			{}
		}
	},
	[ALC275_FIXUP_SONY_VAIO_GPIO2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc275_fixup_gpio4_off,
		.chained = true,
		.chain_id = ALC269_FIXUP_SONY_VAIO
	},
	[ALC269_FIXUP_DELL_M101Z] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 13},
			{0x20, AC_VERB_SET_PROC_COEF, 0x4040},
			{}
		}
	},
	[ALC269_FIXUP_SKU_IGNORE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_sku_ignore,
	},
	[ALC269_FIXUP_ASUS_G73JW] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC269_FIXUP_LENOVO_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC275_FIXUP_SONY_HWEQ] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hweq,
		.chained = true,
		.chain_id = ALC275_FIXUP_SONY_VAIO_GPIO2
	},
	[ALC275_FIXUP_SONY_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_SONY_VAIO
	},
	[ALC271_FIXUP_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc271_fixup_dmic,
	},
	[ALC269_FIXUP_PCM_44K] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pcm_44k,
		.chained = true,
		.chain_id = ALC269_FIXUP_QUANTA_MUTE
	},
	[ALC269_FIXUP_STEREO_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_stereo_dmic,
	},
	[ALC269_FIXUP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_headset_mic,
	},
	[ALC269_FIXUP_QUANTA_MUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_quanta_mute,
	},
	[ALC269_FIXUP_LIFEBOOK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x2101103f }, /* dock line-out */
			{ 0x1b, 0x23a11040 }, /* dock mic-in */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_QUANTA_MUTE
	},
	[ALC269_FIXUP_LIFEBOOK_EXTMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1903c }, /* headset mic, with jack detect */
			{ }
		},
	},
	[ALC269_FIXUP_LIFEBOOK_HP_PIN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x21, 0x0221102f }, /* HP out */
			{ }
		},
	},
	[ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pincfg_no_hp_to_lineout,
	},
	[ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pincfg_U7x7_headset_mic,
	},
	[ALC269_FIXUP_AMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121401f }, /* HP out */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ }
		},
	},
	[ALC269_FIXUP_DMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121401f }, /* HP out */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ }
		},
	},
	[ALC269VB_FIXUP_AMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC269VB_FIXUP_DMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC269_FIXUP_HP_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_mute_led,
	},
	[ALC269_FIXUP_HP_MUTE_LED_MIC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_mute_led_mic1,
	},
	[ALC269_FIXUP_HP_MUTE_LED_MIC2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_mute_led_mic2,
	},
	[ALC269_FIXUP_HP_MUTE_LED_MIC3] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_mute_led_mic3,
		.chained = true,
		.chain_id = ALC295_FIXUP_HP_AUTO_MUTE
	},
	[ALC269_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_gpio_led,
	},
	[ALC269_FIXUP_HP_GPIO_MIC1_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_gpio_mic1_led,
	},
	[ALC269_FIXUP_HP_LINE1_MIC1_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_line1_mic1_led,
	},
	[ALC269_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC269_FIXUP_NO_SHUTUP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_shutup,
	},
	[ALC269_FIXUP_LENOVO_DOCK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x23a11040 }, /* dock mic */
			{ 0x1b, 0x2121103f }, /* dock headphone */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_PINCFG_NO_HP_TO_LINEOUT
	},
	[ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269_FIXUP_LENOVO_DOCK,
	},
	[ALC269_FIXUP_PINCFG_NO_HP_TO_LINEOUT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pincfg_no_hp_to_lineout,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC269_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC269_FIXUP_DELL2_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x21014020 }, /* dock line out */
			{ 0x19, 0x21a19030 }, /* dock mic */
			{ 0x1a, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC269_FIXUP_DELL3_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC269_FIXUP_DELL4_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1b, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC269_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode,
		.chained = true,
		.chain_id = ALC255_FIXUP_MIC_MUTE_LED
	},
	[ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_no_hp_mic,
	},
	[ALC269_FIXUP_ASPIRE_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* headset mic w/o jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE,
	},
	[ALC286_FIXUP_SONY_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC256_FIXUP_HUAWEI_MACH_WX9_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x12, 0x90a60130},
			{0x13, 0x40000000},
			{0x14, 0x90170110},
			{0x18, 0x411111f0},
			{0x19, 0x04a11040},
			{0x1a, 0x411111f0},
			{0x1b, 0x90170112},
			{0x1d, 0x40759a05},
			{0x1e, 0x411111f0},
			{0x21, 0x04211020},
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_MIC_MUTE_LED
	},
	[ALC298_FIXUP_HUAWEI_MBX_STEREO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_huawei_mbx_stereo,
		.chained = true,
		.chain_id = ALC255_FIXUP_MIC_MUTE_LED
	},
	[ALC269_FIXUP_ASUS_X101_FUNC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_x101_headset_mic,
	},
	[ALC269_FIXUP_ASUS_X101_VERB] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x08},
			{0x20, AC_VERB_SET_PROC_COEF,  0x0310},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_ASUS_X101_FUNC
	},
	[ALC269_FIXUP_ASUS_X101] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x04a1182c }, /* Headset mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_ASUS_X101_VERB
	},
	[ALC271_FIXUP_AMIC_MIC2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x19, 0x01a19c20 }, /* mic */
			{ 0x1b, 0x99a7012f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC271_FIXUP_HP_GATE_MIC_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc271_hp_gate_mic_jack,
		.chained = true,
		.chain_id = ALC271_FIXUP_AMIC_MIC2,
	},
	[ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC271_FIXUP_HP_GATE_MIC_JACK,
	},
	[ALC269_FIXUP_ACER_AC700] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x03a11c20 }, /* mic */
			{ 0x1e, 0x0346101e }, /* SPDIF1 */
			{ 0x21, 0x0321101f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC271_FIXUP_DMIC,
	},
	[ALC269_FIXUP_LIMIT_INT_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC269VB_FIXUP_ASUS_ZENBOOK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269VB_FIXUP_DMIC,
	},
	[ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* class-D output amp +5dB */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x12 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2800 },
			{}
		},
		.chained = true,
		.chain_id = ALC269VB_FIXUP_ASUS_ZENBOOK,
	},
	[ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269_FIXUP_HP_MUTE_LED_MIC1,
	},
	[ALC269VB_FIXUP_ORDISSIMO_EVE2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x18, 0x03a11d20 }, /* mic */
			{ 0x19, 0x411111f0 }, /* Unused bogus pin */
			{ }
		},
	},
	[ALC283_FIXUP_CHROME_BOOK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc283_fixup_chromebook,
	},
	[ALC283_FIXUP_SENSE_COMBO_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc283_fixup_sense_combo_jack,
		.chained = true,
		.chain_id = ALC283_FIXUP_CHROME_BOOK,
	},
	[ALC282_FIXUP_ASUS_TX300] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc282_fixup_asus_tx300,
	},
	[ALC283_FIXUP_INT_MIC] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x20, AC_VERB_SET_COEF_INDEX, 0x1a},
			{0x20, AC_VERB_SET_PROC_COEF, 0x0011},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	},
	[ALC290_FIXUP_SUBWOOFER_HSJACK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170112 }, /* subwoofer */
			{ }
		},
		.chained = true,
		.chain_id = ALC290_FIXUP_MONO_SPEAKERS_HSJACK,
	},
	[ALC290_FIXUP_SUBWOOFER] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170112 }, /* subwoofer */
			{ }
		},
		.chained = true,
		.chain_id = ALC290_FIXUP_MONO_SPEAKERS,
	},
	[ALC290_FIXUP_MONO_SPEAKERS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc290_fixup_mono_speakers,
	},
	[ALC290_FIXUP_MONO_SPEAKERS_HSJACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc290_fixup_mono_speakers,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL3_MIC_NO_PRESENCE,
	},
	[ALC269_FIXUP_THINKPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_thinkpad_acpi,
		.chained = true,
		.chain_id = ALC269_FIXUP_SKU_IGNORE,
	},
	[ALC269_FIXUP_DMIC_THINKPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC255_FIXUP_ACER_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_HEADSET_MODE
	},
	[ALC255_FIXUP_ASUS_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_HEADSET_MODE
	},
	[ALC255_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_HEADSET_MODE
	},
	[ALC255_FIXUP_DELL2_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC255_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc255,
		.chained = true,
		.chain_id = ALC255_FIXUP_MIC_MUTE_LED
	},
	[ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc255_no_hp_mic,
	},
	[ALC293_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ 0x1a, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC292_FIXUP_TPT440_DOCK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_tpt440_dock,
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	},
	[ALC292_FIXUP_TPT440] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC292_FIXUP_TPT440_DOCK,
	},
	[ALC283_FIXUP_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x04a110f0 },
			{ },
		},
	},
	[ALC255_FIXUP_MIC_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = snd_hda_gen_fixup_micmute_led,
	},
	[ALC282_FIXUP_ASPIRE_V5_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x90a60130 },
			{ 0x14, 0x90170110 },
			{ 0x17, 0x40000008 },
			{ 0x18, 0x411111f0 },
			{ 0x19, 0x01a1913c },
			{ 0x1a, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1d, 0x40f89b2d },
			{ 0x1e, 0x411111f0 },
			{ 0x21, 0x0321101f },
			{ },
		},
	},
	[ALC280_FIXUP_HP_GPIO4] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc280_fixup_hp_gpio4,
	},
	[ALC286_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc286_fixup_hp_gpio_led,
	},
	[ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc280_fixup_hp_gpio2_mic_hotkey,
	},
	[ALC280_FIXUP_HP_DOCK_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x21011020 }, /* line-out */
			{ 0x1a, 0x01a1903c }, /* headset mic */
			{ 0x18, 0x2181103f }, /* line-in */
			{ },
		},
		.chained = true,
		.chain_id = ALC280_FIXUP_HP_GPIO4
	},
	[ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x21011020 }, /* line-out */
			{ 0x18, 0x2181103f }, /* line-in */
			{ },
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HP_GPIO_MIC1_LED
	},
	[ALC280_FIXUP_HP_9480M] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc280_fixup_hp_9480m,
	},
	[ALC288_FIXUP_DELL_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_dell_alc288,
		.chained = true,
		.chain_id = ALC255_FIXUP_MIC_MUTE_LED
	},
	[ALC288_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC288_FIXUP_DELL_HEADSET_MODE
	},
	[ALC288_FIXUP_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC288_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC288_FIXUP_DELL_XPS_13] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell_xps13,
		.chained = true,
		.chain_id = ALC288_FIXUP_DISABLE_AAMIX
	},
	[ALC292_FIXUP_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL2_MIC_NO_PRESENCE
	},
	[ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC293_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC292_FIXUP_DELL_E7X_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell_xps13,
		.chained = true,
		.chain_id = ALC292_FIXUP_DISABLE_AAMIX
	},
	[ALC292_FIXUP_DELL_E7X] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = snd_hda_gen_fixup_micmute_led,
		/* micmute fixup must be applied at last */
		.chained_before = true,
		.chain_id = ALC292_FIXUP_DELL_E7X_AAMIX,
	},
	[ALC298_FIXUP_ALIENWARE_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* headset mic w/o jack detect */
			{ }
		},
		.chained_before = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE,
	},
	[ALC298_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC275_FIXUP_DELL_XPS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 0x1f},
			{0x20, AC_VERB_SET_PROC_COEF, 0x00c0},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x30},
			{0x20, AC_VERB_SET_PROC_COEF, 0x00b1},
			{}
		}
	},
	[ALC293_FIXUP_LENOVO_SPK_NOISE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI
	},
	[ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc233_fixup_lenovo_line2_mic_hotkey,
	},
	[ALC255_FIXUP_DELL_SPK_NOISE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC225_FIXUP_DISABLE_MIC_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_mic_vref,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC225_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Disable pass-through path for FRONT 14h */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x36 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x57d7 },
			{}
		},
		.chained = true,
		.chain_id = ALC225_FIXUP_DISABLE_MIC_VREF
	},
	[ALC280_FIXUP_HP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC,
	},
	[ALC221_FIXUP_HP_FRONT_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x02a19020 }, /* Front Mic */
			{ }
		},
	},
	[ALC292_FIXUP_TPT460] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_tpt440_dock,
		.chained = true,
		.chain_id = ALC293_FIXUP_LENOVO_SPK_NOISE,
	},
	[ALC298_FIXUP_SPK_VOLUME] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_speaker_volume,
		.chained = true,
		.chain_id = ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE,
	},
	[ALC295_FIXUP_DISABLE_DAC3] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc295_fixup_disable_dac3,
	},
	[ALC285_FIXUP_SPEAKER2_TO_DAC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI
	},
	[ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x90170151 },
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC269_FIXUP_ATIV_BOOK_8] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_auto_mute_via_amp,
		.chained = true,
		.chain_id = ALC269_FIXUP_NO_SHUTUP
	},
	[ALC221_FIXUP_HP_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC256_FIXUP_ASUS_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode,
	},
	[ALC256_FIXUP_ASUS_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x13, 0x90a60160 }, /* use as internal mic */
			{ 0x19, 0x04a11120 }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC256_FIXUP_ASUS_HEADSET_MODE
	},
	[ALC256_FIXUP_ASUS_AIO_GPIO2] = {
		.type = HDA_FIXUP_FUNC,
		/* Set up GPIO2 for the speaker amp */
		.v.func = alc_fixup_gpio4,
	},
	[ALC233_FIXUP_ASUS_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 0x40},
			{0x20, AC_VERB_SET_PROC_COEF, 0x8800},
			{}
		},
		.chained = true,
		.chain_id = ALC233_FIXUP_ASUS_MIC_NO_PRESENCE
	},
	[ALC233_FIXUP_LENOVO_MULTI_CODECS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc233_alc662_fixup_lenovo_dual_codecs,
	},
	[ALC233_FIXUP_ACER_HEADSET_MIC] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x45 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x5089 },
			{ }
		},
		.chained = true,
		.chain_id = ALC233_FIXUP_ASUS_MIC_NO_PRESENCE
	},
	[ALC294_FIXUP_LENOVO_MIC_LOCATION] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* Change the mic location from front to right, otherwise there are
			   two front mics with the same name, pulseaudio can't handle them.
			   This is just a temporary workaround, after applying this fixup,
			   there will be one "Front Mic" and one "Mic" in this machine.
			 */
			{ 0x1a, 0x04a19040 },
			{ }
		},
	},
	[ALC225_FIXUP_DELL_WYSE_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x0101102f }, /* Rear Headset HP */
			{ 0x19, 0x02a1913c }, /* use as Front headset mic, without its own jack detect */
			{ 0x1a, 0x01a19030 }, /* Rear Headset MIC */
			{ 0x1b, 0x02011020 },
			{ }
		},
		.chained = true,
		.chain_id = ALC225_FIXUP_S3_POP_NOISE
	},
	[ALC225_FIXUP_S3_POP_NOISE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc225_fixup_s3_pop_noise,
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC700_FIXUP_INTEL_REFERENCE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 0x45},
			{0x20, AC_VERB_SET_PROC_COEF, 0x5289},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x4A},
			{0x20, AC_VERB_SET_PROC_COEF, 0x001b},
			{0x58, AC_VERB_SET_COEF_INDEX, 0x00},
			{0x58, AC_VERB_SET_PROC_COEF, 0x3888},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x6f},
			{0x20, AC_VERB_SET_PROC_COEF, 0x2c0b},
			{}
		}
	},
	[ALC274_FIXUP_DELL_BIND_DACS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc274_fixup_bind_dacs,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC274_FIXUP_DELL_AIO_LINEOUT_VERB] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x0401102f },
			{ }
		},
		.chained = true,
		.chain_id = ALC274_FIXUP_DELL_BIND_DACS
	},
	[ALC298_FIXUP_TPT470_DOCK_FIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_tpt470_dock,
		.chained = true,
		.chain_id = ALC293_FIXUP_LENOVO_SPK_NOISE
	},
	[ALC298_FIXUP_TPT470_DOCK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_tpt470_dacs,
		.chained = true,
		.chain_id = ALC298_FIXUP_TPT470_DOCK_FIX
	},
	[ALC255_FIXUP_DUMMY_LINEOUT_VERB] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0201101f },
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC255_FIXUP_DELL_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC295_FIXUP_HP_X360] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc295_fixup_hp_top_speakers,
		.chained = true,
		.chain_id = ALC269_FIXUP_HP_MUTE_LED_MIC3
	},
	[ALC221_FIXUP_HP_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x0181313f},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC285_FIXUP_LENOVO_HEADPHONE_NOISE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_invalidate_dacs,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI
	},
	[ALC295_FIXUP_HP_AUTO_MUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_auto_mute_via_amp,
	},
	[ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC294_FIXUP_ASUS_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x13, 0x90a60160 }, /* use as internal mic */
			{ 0x19, 0x04a11120 }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC294_FIXUP_ASUS_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1103c }, /* use as headset mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC294_FIXUP_ASUS_SPK] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Set EAPD high */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x40 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x8800 },
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_HEADSET_MIC
	},
	[ALC295_FIXUP_CHROME_BOOK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc295_fixup_chromebook,
		.chained = true,
		.chain_id = ALC225_FIXUP_HEADSET_JACK
	},
	[ALC225_FIXUP_HEADSET_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_jack,
	},
	[ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC285_FIXUP_LENOVO_PC_BEEP_IN_NOISE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Disable PCBEEP-IN passthrough */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x36 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x57d7 },
			{ }
		},
		.chained = true,
		.chain_id = ALC285_FIXUP_LENOVO_HEADPHONE_NOISE
	},
	[ALC255_FIXUP_ACER_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11130 },
			{ 0x1a, 0x90a60140 }, /* use as internal mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC225_FIXUP_DELL_WYSE_AIO_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x01011020 }, /* Rear Line out */
			{ 0x19, 0x01a1913c }, /* use as Front headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC225_FIXUP_WYSE_AUTO_MUTE
	},
	[ALC225_FIXUP_WYSE_AUTO_MUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_auto_mute_via_amp,
		.chained = true,
		.chain_id = ALC225_FIXUP_WYSE_DISABLE_MIC_VREF
	},
	[ALC225_FIXUP_WYSE_DISABLE_MIC_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_mic_vref,
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC286_FIXUP_ACER_AIO_HEADSET_MIC] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x4f },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x5029 },
			{ }
		},
		.chained = true,
		.chain_id = ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE
	},
	[ALC256_FIXUP_ASUS_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11020 }, /* headset mic with jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC256_FIXUP_ASUS_HEADSET_MODE
	},
	[ALC256_FIXUP_ASUS_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x04a11120 }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC256_FIXUP_ASUS_HEADSET_MODE
	},
	[ALC299_FIXUP_PREDATOR_SPK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x21, 0x90170150 }, /* use as headset mic, without its own jack detect */
			{ }
		}
	},
	[ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x04a11040 },
			{ 0x21, 0x04211020 },
			{ }
		},
		.chained = true,
		.chain_id = ALC256_FIXUP_ASUS_HEADSET_MODE
	},
	[ALC289_FIXUP_DELL_SPK2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170130 }, /* bass spk */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL4_MIC_NO_PRESENCE
	},
	[ALC289_FIXUP_DUAL_SPK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC289_FIXUP_DELL_SPK2
	},
	[ALC294_FIXUP_SPK2_TO_DAC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_HEADSET_MIC
	},
	[ALC294_FIXUP_ASUS_DUAL_SPK] = {
		.type = HDA_FIXUP_FUNC,
		/* The GPIO must be pulled to initialize the AMP */
		.v.func = alc_fixup_gpio4,
		.chained = true,
		.chain_id = ALC294_FIXUP_SPK2_TO_DAC1
	},
	[ALC285_FIXUP_THINKPAD_HEADSET_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_jack,
		.chained = true,
		.chain_id = ALC285_FIXUP_SPEAKER2_TO_DAC1
	},
	[ALC294_FIXUP_ASUS_HPE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Set EAPD high */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0f },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x7774 },
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_HEADSET_MIC
	},
	[ALC294_FIXUP_ASUS_COEF_1B] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Set bit 10 to correct noisy output after reboot from
			 * Windows 10 (due to pop noise reduction?)
			 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x1b },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x4e4b },
			{ }
		},
	},
	[ALC285_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_gpio_led,
	},
	[ALC285_FIXUP_HP_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_mute_led,
	},
	[ALC236_FIXUP_HP_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc236_fixup_hp_mute_led,
	},
	[ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc5 },
			{ }
		},
	},
	[ALC295_FIXUP_ASUS_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
};

static const struct snd_pci_quirk alc269_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0283, "Acer TravelMate 8371", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x029b, "Acer 1810TZ", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0349, "Acer AOD260", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x047c, "Acer AC700", ALC269_FIXUP_ACER_AC700),
	SND_PCI_QUIRK(0x1025, 0x072d, "Acer Aspire V5-571G", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x080d, "Acer Aspire V5-122P", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x0740, "Acer AO725", ALC271_FIXUP_HP_GATE_MIC_JACK),
	SND_PCI_QUIRK(0x1025, 0x0742, "Acer AO756", ALC271_FIXUP_HP_GATE_MIC_JACK),
	SND_PCI_QUIRK(0x1025, 0x0762, "Acer Aspire E1-472", ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572),
	SND_PCI_QUIRK(0x1025, 0x0775, "Acer Aspire E1-572", ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572),
	SND_PCI_QUIRK(0x1025, 0x079b, "Acer Aspire V5-573G", ALC282_FIXUP_ASPIRE_V5_PINS),
	SND_PCI_QUIRK(0x1025, 0x102b, "Acer Aspire C24-860", ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x106d, "Acer Cloudbook 14", ALC283_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x1025, 0x1099, "Acer Aspire E5-523G", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x110e, "Acer Aspire ES1-432", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1246, "Acer Predator Helios 500", ALC299_FIXUP_PREDATOR_SPK),
	SND_PCI_QUIRK(0x1025, 0x128f, "Acer Veriton Z6860G", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1290, "Acer Veriton Z4860G", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1291, "Acer Veriton Z4660G", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1308, "Acer Aspire Z24-890", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x132a, "Acer TravelMate B114-21", ALC233_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1330, "Acer TravelMate X514-51T", ALC255_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0470, "Dell M101z", ALC269_FIXUP_DELL_M101Z),
	SND_PCI_QUIRK(0x1028, 0x054b, "Dell XPS one 2710", ALC275_FIXUP_DELL_XPS),
	SND_PCI_QUIRK(0x1028, 0x05bd, "Dell Latitude E6440", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05be, "Dell Latitude E6540", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05ca, "Dell Latitude E7240", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05cb, "Dell Latitude E7440", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05da, "Dell Vostro 5460", ALC290_FIXUP_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x05f4, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05f5, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05f6, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0615, "Dell Vostro 5470", ALC290_FIXUP_SUBWOOFER_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x0616, "Dell Vostro 5470", ALC290_FIXUP_SUBWOOFER_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x062c, "Dell Latitude E5550", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x062e, "Dell Latitude E7450", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x0638, "Dell Inspiron 5439", ALC290_FIXUP_MONO_SPEAKERS_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x064a, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x064b, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0665, "Dell XPS 13", ALC288_FIXUP_DELL_XPS_13),
	SND_PCI_QUIRK(0x1028, 0x0669, "Dell Optiplex 9020m", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x069a, "Dell Vostro 5480", ALC290_FIXUP_SUBWOOFER_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x06c7, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x06d9, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x06da, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x06db, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06dd, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06de, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06df, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06e0, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x0706, "Dell Inspiron 7559", ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x0725, "Dell Inspiron 3162", ALC255_FIXUP_DELL_SPK_NOISE),
	SND_PCI_QUIRK(0x1028, 0x0738, "Dell Precision 5820", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1028, 0x075c, "Dell XPS 27 7760", ALC298_FIXUP_SPK_VOLUME),
	SND_PCI_QUIRK(0x1028, 0x075d, "Dell AIO", ALC298_FIXUP_SPK_VOLUME),
	SND_PCI_QUIRK(0x1028, 0x07b0, "Dell Precision 7520", ALC295_FIXUP_DISABLE_DAC3),
	SND_PCI_QUIRK(0x1028, 0x0798, "Dell Inspiron 17 7000 Gaming", ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x080c, "Dell WYSE", ALC225_FIXUP_DELL_WYSE_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x084b, "Dell", ALC274_FIXUP_DELL_AIO_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x084e, "Dell", ALC274_FIXUP_DELL_AIO_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x0871, "Dell Precision 3630", ALC255_FIXUP_DELL_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0872, "Dell Precision 3630", ALC255_FIXUP_DELL_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0873, "Dell Precision 3930", ALC255_FIXUP_DUMMY_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x08ad, "Dell WYSE AIO", ALC225_FIXUP_DELL_WYSE_AIO_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x08ae, "Dell WYSE NB", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0935, "Dell", ALC274_FIXUP_DELL_AIO_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x097e, "Dell Precision", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x097d, "Dell Precision", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x098d, "Dell Precision", ALC233_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x09bf, "Dell Precision", ALC233_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x164a, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x164b, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x1586, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC2),
	SND_PCI_QUIRK(0x103c, 0x18e6, "HP", ALC269_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x218b, "HP", ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x225f, "HP", ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY),
	/* ALC282 */
	SND_PCI_QUIRK(0x103c, 0x21f9, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2210, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2214, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2236, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2237, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2238, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2239, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x224b, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2268, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226a, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226b, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2271, "HP", ALC286_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x2272, "HP", ALC280_FIXUP_HP_DOCK_PINS),
	SND_PCI_QUIRK(0x103c, 0x2273, "HP", ALC280_FIXUP_HP_DOCK_PINS),
	SND_PCI_QUIRK(0x103c, 0x229e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22b2, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22b7, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22bf, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22cf, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22db, "HP", ALC280_FIXUP_HP_9480M),
	SND_PCI_QUIRK(0x103c, 0x22dc, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x22fb, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	/* ALC290 */
	SND_PCI_QUIRK(0x103c, 0x221b, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2221, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2225, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2253, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2254, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2255, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2256, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2257, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2259, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x225a, "HP", ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2260, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2263, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2264, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2265, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2272, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2273, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2278, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x227f, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2282, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x228b, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x228e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c5, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c7, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c8, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c4, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2334, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2335, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2336, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2337, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x221c, "HP EliteBook 755 G2", ALC280_FIXUP_HP_HEADSET_MIC),
	SND_PCI_QUIRK(0x103c, 0x802e, "HP Z240 SFF", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x802f, "HP Z240", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x820d, "HP Pavilion 15", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x8256, "HP", ALC221_FIXUP_HP_FRONT_MIC),
	SND_PCI_QUIRK(0x103c, 0x827e, "HP x360", ALC295_FIXUP_HP_X360),
	SND_PCI_QUIRK(0x103c, 0x82bf, "HP G3 mini", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x82c0, "HP G3 mini premium", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x83b9, "HP Spectre x360", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x8497, "HP Envy x360", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x84e7, "HP Pavilion 15", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x8736, "HP", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x877a, "HP", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x877d, "HP", ALC236_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x1043, 0x103e, "ASUS X540SA", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x103f, "ASUS TX300", ALC282_FIXUP_ASUS_TX300),
	SND_PCI_QUIRK(0x1043, 0x106d, "Asus K53BE", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x10a1, "ASUS UX391UA", ALC294_FIXUP_ASUS_SPK),
	SND_PCI_QUIRK(0x1043, 0x10c0, "ASUS X540SA", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x10d0, "ASUS X540LA/X540LJ", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x115d, "Asus 1015E", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x11c0, "ASUS X556UR", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1290, "ASUS X441SA", ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x12a0, "ASUS X441UV", ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x12f0, "ASUS X541UV", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x12e0, "ASUS X541SA", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x13b0, "ASUS Z550SA", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x1427, "Asus Zenbook UX31E", ALC269VB_FIXUP_ASUS_ZENBOOK),
	SND_PCI_QUIRK(0x1043, 0x1517, "Asus Zenbook UX31A", ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A),
	SND_PCI_QUIRK(0x1043, 0x16e3, "ASUS UX50", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x17d1, "ASUS UX431FL", ALC294_FIXUP_ASUS_DUAL_SPK),
	SND_PCI_QUIRK(0x1043, 0x18b1, "Asus MJ401TA", ALC256_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x18f1, "Asus FX505DT", ALC256_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x19ce, "ASUS B9450FA", ALC294_FIXUP_ASUS_HPE),
	SND_PCI_QUIRK(0x1043, 0x19e1, "ASUS UX581LV", ALC295_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1a13, "Asus G73Jw", ALC269_FIXUP_ASUS_G73JW),
	SND_PCI_QUIRK(0x1043, 0x1a30, "ASUS X705UD", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x1b11, "ASUS UX431DA", ALC294_FIXUP_ASUS_COEF_1B),
	SND_PCI_QUIRK(0x1043, 0x1b13, "Asus U41SV", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1043, 0x1bbd, "ASUS Z550MA", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1c23, "Asus X55U", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x1ccd, "ASUS X555UB", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x3030, "ASUS ZN270IE", ALC256_FIXUP_ASUS_AIO_GPIO2),
	SND_PCI_QUIRK(0x1043, 0x831a, "ASUS P901", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x834a, "ASUS S101", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8398, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x83ce, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8516, "ASUS X101CH", ALC269_FIXUP_ASUS_X101),
	SND_PCI_QUIRK(0x104d, 0x90b5, "Sony VAIO Pro 11", ALC286_FIXUP_SONY_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x104d, 0x90b6, "Sony VAIO Pro 13", ALC286_FIXUP_SONY_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x104d, 0x9073, "Sony VAIO", ALC275_FIXUP_SONY_VAIO_GPIO2),
	SND_PCI_QUIRK(0x104d, 0x907b, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9084, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9099, "Sony VAIO S13", ALC275_FIXUP_SONY_DISABLE_AAMIX),
	SND_PCI_QUIRK(0x10cf, 0x1475, "Lifebook", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x10cf, 0x159f, "Lifebook E780", ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT),
	SND_PCI_QUIRK(0x10cf, 0x15dc, "Lifebook T731", ALC269_FIXUP_LIFEBOOK_HP_PIN),
	SND_PCI_QUIRK(0x10cf, 0x1757, "Lifebook E752", ALC269_FIXUP_LIFEBOOK_HP_PIN),
	SND_PCI_QUIRK(0x10cf, 0x1629, "Lifebook U7x7", ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC),
	SND_PCI_QUIRK(0x10cf, 0x1845, "Lifebook U904", ALC269_FIXUP_LIFEBOOK_EXTMIC),
	SND_PCI_QUIRK(0x10ec, 0x10f2, "Intel Reference board", ALC700_FIXUP_INTEL_REFERENCE),
	SND_PCI_QUIRK(0x10f7, 0x8338, "Panasonic CF-SZ6", ALC269_FIXUP_HEADSET_MODE),
	SND_PCI_QUIRK(0x144d, 0xc109, "Samsung Ativ book 9 (NP900X3G)", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x144d, 0xc169, "Samsung Notebook 9 Pen (NP930SBE-K01US)", ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET),
	SND_PCI_QUIRK(0x144d, 0xc176, "Samsung Notebook 9 Pro (NP930MBE-K04US)", ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET),
	SND_PCI_QUIRK(0x144d, 0xc740, "Samsung Ativ book 8 (NP870Z5G)", ALC269_FIXUP_ATIV_BOOK_8),
	SND_PCI_QUIRK(0x1458, 0xfa53, "Gigabyte BXBT-2807", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1462, 0xb120, "MSI Cubi MS-B120", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1462, 0xb171, "Cubi N 8GL (MS-B171)", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1558, 0x1325, "System76 Darter Pro (darp5)", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8550, "System76 Gazelle (gaze14)", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8551, "System76 Gazelle (gaze14)", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8560, "System76 Gazelle (gaze14)", ALC269_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1558, 0x8561, "System76 Gazelle (gaze14)", ALC269_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x17aa, 0x1036, "Lenovo P520", ALC233_FIXUP_LENOVO_MULTI_CODECS),
	SND_PCI_QUIRK(0x17aa, 0x1048, "ThinkCentre Station", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Thinkpad SL410/510", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Thinkpad L512", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21b8, "Thinkpad Edge 14", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21ca, "Thinkpad L412", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21e9, "Thinkpad Edge 15", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21f6, "Thinkpad T530", ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x21fa, "Thinkpad X230", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x21f3, "Thinkpad T430", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x21fb, "Thinkpad T430s", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2203, "Thinkpad X230 Tablet", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2208, "Thinkpad T431s", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x220c, "Thinkpad T440s", ALC292_FIXUP_TPT440),
	SND_PCI_QUIRK(0x17aa, 0x220e, "Thinkpad T440p", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2210, "Thinkpad T540p", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2211, "Thinkpad W541", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2212, "Thinkpad T440", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2214, "Thinkpad X240", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2215, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x2218, "Thinkpad X1 Carbon 2nd", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2223, "ThinkPad T550", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2226, "ThinkPad X250", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x222d, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x222e, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2231, "Thinkpad T560", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x2233, "Thinkpad", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x2245, "Thinkpad T470", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2246, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2247, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2249, "Thinkpad", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x224b, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x224c, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x224d, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x225d, "Thinkpad T480", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x2292, "Thinkpad X1 Yoga 7th", ALC285_FIXUP_THINKPAD_HEADSET_JACK),
	SND_PCI_QUIRK(0x17aa, 0x2293, "Thinkpad X1 Carbon 7th", ALC285_FIXUP_THINKPAD_HEADSET_JACK),
	SND_PCI_QUIRK(0x17aa, 0x22be, "Thinkpad X1 Carbon 8th", ALC285_FIXUP_THINKPAD_HEADSET_JACK),
	SND_PCI_QUIRK(0x17aa, 0x30bb, "ThinkCentre AIO", ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY),
	SND_PCI_QUIRK(0x17aa, 0x30e2, "ThinkCentre AIO", ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY),
	SND_PCI_QUIRK(0x17aa, 0x310c, "ThinkCentre Station", ALC294_FIXUP_LENOVO_MIC_LOCATION),
	SND_PCI_QUIRK(0x17aa, 0x3111, "ThinkCentre Station", ALC294_FIXUP_LENOVO_MIC_LOCATION),
	SND_PCI_QUIRK(0x17aa, 0x312a, "ThinkCentre Station", ALC294_FIXUP_LENOVO_MIC_LOCATION),
	SND_PCI_QUIRK(0x17aa, 0x312f, "ThinkCentre Station", ALC294_FIXUP_LENOVO_MIC_LOCATION),
	SND_PCI_QUIRK(0x17aa, 0x313c, "ThinkCentre Station", ALC294_FIXUP_LENOVO_MIC_LOCATION),
	SND_PCI_QUIRK(0x17aa, 0x3151, "ThinkCentre Station", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3176, "ThinkCentre Station", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3178, "ThinkCentre Station", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3902, "Lenovo E50-80", ALC269_FIXUP_DMIC_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x17aa, 0x3977, "IdeaPad S210", ALC283_FIXUP_INT_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3978, "Lenovo B50-70", ALC269_FIXUP_DMIC_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x17aa, 0x5013, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x501a, "Thinkpad", ALC283_FIXUP_INT_MIC),
	SND_PCI_QUIRK(0x17aa, 0x501e, "Thinkpad L440", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x5026, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x5034, "Thinkpad T450", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x5036, "Thinkpad T450s", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x503c, "Thinkpad L450", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x504a, "ThinkPad X260", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x504b, "Thinkpad", ALC293_FIXUP_LENOVO_SPK_NOISE),
	SND_PCI_QUIRK(0x17aa, 0x5050, "Thinkpad T560p", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x5051, "Thinkpad L460", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x5053, "Thinkpad T460", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x505d, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x505f, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x5062, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x5109, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x511e, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x511f, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_FIXUP_PCM_44K),
	SND_PCI_QUIRK(0x17aa, 0x9e54, "LENOVO NB", ALC269_FIXUP_LENOVO_EAPD),
	SND_PCI_QUIRK(0x19e5, 0x3204, "Huawei MACH-WX9", ALC256_FIXUP_HUAWEI_MACH_WX9_PINS),
	SND_PCI_QUIRK(0x1b7d, 0xa831, "Ordissimo EVE2 ", ALC269VB_FIXUP_ORDISSIMO_EVE2), /* Also known as Malata PC-B1303 */
	SND_PCI_QUIRK(0x1d72, 0x1901, "RedmiBook 14", ALC256_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x10ec, 0x118c, "Medion EE4254 MD62100", ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE),

#if 0
	/* Below is a quirk table taken from the old code.
	 * Basically the device should work as is without the fixup table.
	 * If BIOS doesn't give a proper info, enable the corresponding
	 * fixup entry.
	 */
	SND_PCI_QUIRK(0x1043, 0x8330, "ASUS Eeepc P703 P900A",
		      ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1013, "ASUS N61Da", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1143, "ASUS B53f", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1133, "ASUS UJ20ft", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1183, "ASUS K72DR", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11b3, "ASUS K52DR", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11e3, "ASUS U33Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1273, "ASUS UL80Jt", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1283, "ASUS U53Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12b3, "ASUS N82JV", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12d3, "ASUS N61Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13a3, "ASUS UL30Vt", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1373, "ASUS G73JX", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1383, "ASUS UJ30Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13d3, "ASUS N61JA", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1413, "ASUS UL50", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1443, "ASUS UL30", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1453, "ASUS M60Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1483, "ASUS UL80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14f3, "ASUS F83Vf", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14e3, "ASUS UL20", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1513, "ASUS UX30", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1593, "ASUS N51Vn", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15a3, "ASUS N60Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15b3, "ASUS N60Dp", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15c3, "ASUS N70De", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15e3, "ASUS F83T", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1643, "ASUS M60J", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1653, "ASUS U50", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1693, "ASUS F50N", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x16a3, "ASUS F5Q", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1723, "ASUS P80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1743, "ASUS U80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1773, "ASUS U20A", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1883, "ASUS F81Se", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x152d, 0x1778, "Quanta ON1", ALC269_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x3be9, "Quanta Wistron", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x17ff, 0x059a, "Quanta EL3", ALC269_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17ff, 0x059b, "Quanta JR1", ALC269_FIXUP_DMIC),
#endif
	{}
};

static const struct snd_pci_quirk alc269_fixup_vendor_tbl[] = {
	SND_PCI_QUIRK_VENDOR(0x1025, "Acer Aspire", ALC271_FIXUP_DMIC),
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK_VENDOR(0x104d, "Sony VAIO", ALC269_FIXUP_SONY_VAIO),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Thinkpad", ALC269_FIXUP_THINKPAD_ACPI),
	SND_PCI_QUIRK_VENDOR(0x19e5, "Huawei Matebook", ALC255_FIXUP_MIC_MUTE_LED),
	{}
};

static const struct hda_model_fixup alc269_fixup_models[] = {
	{.id = ALC269_FIXUP_AMIC, .name = "laptop-amic"},
	{.id = ALC269_FIXUP_DMIC, .name = "laptop-dmic"},
	{.id = ALC269_FIXUP_STEREO_DMIC, .name = "alc269-dmic"},
	{.id = ALC271_FIXUP_DMIC, .name = "alc271-dmic"},
	{.id = ALC269_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC269_FIXUP_HEADSET_MIC, .name = "headset-mic"},
	{.id = ALC269_FIXUP_HEADSET_MODE, .name = "headset-mode"},
	{.id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC, .name = "headset-mode-no-hp-mic"},
	{.id = ALC269_FIXUP_LENOVO_DOCK, .name = "lenovo-dock"},
	{.id = ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST, .name = "lenovo-dock-limit-boost"},
	{.id = ALC269_FIXUP_HP_GPIO_LED, .name = "hp-gpio-led"},
	{.id = ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED, .name = "hp-dock-gpio-mic1-led"},
	{.id = ALC269_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "dell-headset-multi"},
	{.id = ALC269_FIXUP_DELL2_MIC_NO_PRESENCE, .name = "dell-headset-dock"},
	{.id = ALC269_FIXUP_DELL3_MIC_NO_PRESENCE, .name = "dell-headset3"},
	{.id = ALC269_FIXUP_DELL4_MIC_NO_PRESENCE, .name = "dell-headset4"},
	{.id = ALC283_FIXUP_CHROME_BOOK, .name = "alc283-dac-wcaps"},
	{.id = ALC283_FIXUP_SENSE_COMBO_JACK, .name = "alc283-sense-combo"},
	{.id = ALC292_FIXUP_TPT440_DOCK, .name = "tpt440-dock"},
	{.id = ALC292_FIXUP_TPT440, .name = "tpt440"},
	{.id = ALC292_FIXUP_TPT460, .name = "tpt460"},
	{.id = ALC298_FIXUP_TPT470_DOCK_FIX, .name = "tpt470-dock-fix"},
	{.id = ALC298_FIXUP_TPT470_DOCK, .name = "tpt470-dock"},
	{.id = ALC233_FIXUP_LENOVO_MULTI_CODECS, .name = "dual-codecs"},
	{.id = ALC700_FIXUP_INTEL_REFERENCE, .name = "alc700-ref"},
	{.id = ALC269_FIXUP_SONY_VAIO, .name = "vaio"},
	{.id = ALC269_FIXUP_DELL_M101Z, .name = "dell-m101z"},
	{.id = ALC269_FIXUP_ASUS_G73JW, .name = "asus-g73jw"},
	{.id = ALC269_FIXUP_LENOVO_EAPD, .name = "lenovo-eapd"},
	{.id = ALC275_FIXUP_SONY_HWEQ, .name = "sony-hweq"},
	{.id = ALC269_FIXUP_PCM_44K, .name = "pcm44k"},
	{.id = ALC269_FIXUP_LIFEBOOK, .name = "lifebook"},
	{.id = ALC269_FIXUP_LIFEBOOK_EXTMIC, .name = "lifebook-extmic"},
	{.id = ALC269_FIXUP_LIFEBOOK_HP_PIN, .name = "lifebook-hp-pin"},
	{.id = ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC, .name = "lifebook-u7x7"},
	{.id = ALC269VB_FIXUP_AMIC, .name = "alc269vb-amic"},
	{.id = ALC269VB_FIXUP_DMIC, .name = "alc269vb-dmic"},
	{.id = ALC269_FIXUP_HP_MUTE_LED_MIC1, .name = "hp-mute-led-mic1"},
	{.id = ALC269_FIXUP_HP_MUTE_LED_MIC2, .name = "hp-mute-led-mic2"},
	{.id = ALC269_FIXUP_HP_MUTE_LED_MIC3, .name = "hp-mute-led-mic3"},
	{.id = ALC269_FIXUP_HP_GPIO_MIC1_LED, .name = "hp-gpio-mic1"},
	{.id = ALC269_FIXUP_HP_LINE1_MIC1_LED, .name = "hp-line1-mic1"},
	{.id = ALC269_FIXUP_NO_SHUTUP, .name = "noshutup"},
	{.id = ALC286_FIXUP_SONY_MIC_NO_PRESENCE, .name = "sony-nomic"},
	{.id = ALC269_FIXUP_ASPIRE_HEADSET_MIC, .name = "aspire-headset-mic"},
	{.id = ALC269_FIXUP_ASUS_X101, .name = "asus-x101"},
	{.id = ALC271_FIXUP_HP_GATE_MIC_JACK, .name = "acer-ao7xx"},
	{.id = ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572, .name = "acer-aspire-e1"},
	{.id = ALC269_FIXUP_ACER_AC700, .name = "acer-ac700"},
	{.id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST, .name = "limit-mic-boost"},
	{.id = ALC269VB_FIXUP_ASUS_ZENBOOK, .name = "asus-zenbook"},
	{.id = ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A, .name = "asus-zenbook-ux31a"},
	{.id = ALC269VB_FIXUP_ORDISSIMO_EVE2, .name = "ordissimo"},
	{.id = ALC282_FIXUP_ASUS_TX300, .name = "asus-tx300"},
	{.id = ALC283_FIXUP_INT_MIC, .name = "alc283-int-mic"},
	{.id = ALC290_FIXUP_MONO_SPEAKERS_HSJACK, .name = "mono-speakers"},
	{.id = ALC290_FIXUP_SUBWOOFER_HSJACK, .name = "alc290-subwoofer"},
	{.id = ALC269_FIXUP_THINKPAD_ACPI, .name = "thinkpad"},
	{.id = ALC269_FIXUP_DMIC_THINKPAD_ACPI, .name = "dmic-thinkpad"},
	{.id = ALC255_FIXUP_ACER_MIC_NO_PRESENCE, .name = "alc255-acer"},
	{.id = ALC255_FIXUP_ASUS_MIC_NO_PRESENCE, .name = "alc255-asus"},
	{.id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "alc255-dell1"},
	{.id = ALC255_FIXUP_DELL2_MIC_NO_PRESENCE, .name = "alc255-dell2"},
	{.id = ALC293_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "alc293-dell1"},
	{.id = ALC283_FIXUP_HEADSET_MIC, .name = "alc283-headset"},
	{.id = ALC255_FIXUP_MIC_MUTE_LED, .name = "alc255-dell-mute"},
	{.id = ALC282_FIXUP_ASPIRE_V5_PINS, .name = "aspire-v5"},
	{.id = ALC280_FIXUP_HP_GPIO4, .name = "hp-gpio4"},
	{.id = ALC286_FIXUP_HP_GPIO_LED, .name = "hp-gpio-led"},
	{.id = ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY, .name = "hp-gpio2-hotkey"},
	{.id = ALC280_FIXUP_HP_DOCK_PINS, .name = "hp-dock-pins"},
	{.id = ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED, .name = "hp-dock-gpio-mic"},
	{.id = ALC280_FIXUP_HP_9480M, .name = "hp-9480m"},
	{.id = ALC288_FIXUP_DELL_HEADSET_MODE, .name = "alc288-dell-headset"},
	{.id = ALC288_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "alc288-dell1"},
	{.id = ALC288_FIXUP_DELL_XPS_13, .name = "alc288-dell-xps13"},
	{.id = ALC292_FIXUP_DELL_E7X, .name = "dell-e7x"},
	{.id = ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK, .name = "alc293-dell"},
	{.id = ALC298_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "alc298-dell1"},
	{.id = ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE, .name = "alc298-dell-aio"},
	{.id = ALC275_FIXUP_DELL_XPS, .name = "alc275-dell-xps"},
	{.id = ALC293_FIXUP_LENOVO_SPK_NOISE, .name = "lenovo-spk-noise"},
	{.id = ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY, .name = "lenovo-hotkey"},
	{.id = ALC255_FIXUP_DELL_SPK_NOISE, .name = "dell-spk-noise"},
	{.id = ALC225_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "alc225-dell1"},
	{.id = ALC295_FIXUP_DISABLE_DAC3, .name = "alc295-disable-dac3"},
	{.id = ALC285_FIXUP_SPEAKER2_TO_DAC1, .name = "alc285-speaker2-to-dac1"},
	{.id = ALC280_FIXUP_HP_HEADSET_MIC, .name = "alc280-hp-headset"},
	{.id = ALC221_FIXUP_HP_FRONT_MIC, .name = "alc221-hp-mic"},
	{.id = ALC298_FIXUP_SPK_VOLUME, .name = "alc298-spk-volume"},
	{.id = ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER, .name = "dell-inspiron-7559"},
	{.id = ALC269_FIXUP_ATIV_BOOK_8, .name = "ativ-book"},
	{.id = ALC221_FIXUP_HP_MIC_NO_PRESENCE, .name = "alc221-hp-mic"},
	{.id = ALC256_FIXUP_ASUS_HEADSET_MODE, .name = "alc256-asus-headset"},
	{.id = ALC256_FIXUP_ASUS_MIC, .name = "alc256-asus-mic"},
	{.id = ALC256_FIXUP_ASUS_AIO_GPIO2, .name = "alc256-asus-aio"},
	{.id = ALC233_FIXUP_ASUS_MIC_NO_PRESENCE, .name = "alc233-asus"},
	{.id = ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE, .name = "alc233-eapd"},
	{.id = ALC294_FIXUP_LENOVO_MIC_LOCATION, .name = "alc294-lenovo-mic"},
	{.id = ALC225_FIXUP_DELL_WYSE_MIC_NO_PRESENCE, .name = "alc225-wyse"},
	{.id = ALC274_FIXUP_DELL_AIO_LINEOUT_VERB, .name = "alc274-dell-aio"},
	{.id = ALC255_FIXUP_DUMMY_LINEOUT_VERB, .name = "alc255-dummy-lineout"},
	{.id = ALC255_FIXUP_DELL_HEADSET_MIC, .name = "alc255-dell-headset"},
	{.id = ALC295_FIXUP_HP_X360, .name = "alc295-hp-x360"},
	{.id = ALC225_FIXUP_HEADSET_JACK, .name = "alc-headset-jack"},
	{.id = ALC295_FIXUP_CHROME_BOOK, .name = "alc-chrome-book"},
	{.id = ALC299_FIXUP_PREDATOR_SPK, .name = "predator-spk"},
	{.id = ALC298_FIXUP_HUAWEI_MBX_STEREO, .name = "huawei-mbx-stereo"},
	{.id = ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE, .name = "alc256-medion-headset"},
	{}
};
#define ALC225_STANDARD_PINS \
	{0x21, 0x04211020}

#define ALC256_STANDARD_PINS \
	{0x12, 0x90a60140}, \
	{0x14, 0x90170110}, \
	{0x21, 0x02211020}

#define ALC282_STANDARD_PINS \
	{0x14, 0x90170110}

#define ALC290_STANDARD_PINS \
	{0x12, 0x99a30130}

#define ALC292_STANDARD_PINS \
	{0x14, 0x90170110}, \
	{0x15, 0x0221401f}

#define ALC295_STANDARD_PINS \
	{0x12, 0xb7a60130}, \
	{0x14, 0x90170110}, \
	{0x21, 0x04211020}

#define ALC298_STANDARD_PINS \
	{0x12, 0x90a60130}, \
	{0x21, 0x03211020}

static const struct snd_hda_pin_quirk alc269_pin_fixup_tbl[] = {
	SND_HDA_PIN_QUIRK(0x10ec0221, 0x103c, "HP Workstation", ALC221_FIXUP_HP_HEADSET_MIC,
		{0x14, 0x01014020},
		{0x17, 0x90170110},
		{0x18, 0x02a11030},
		{0x19, 0x0181303F},
		{0x21, 0x0221102f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1025, "Acer", ALC255_FIXUP_ACER_MIC_NO_PRESENCE,
		{0x12, 0x90a601c0},
		{0x14, 0x90171120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1043, "ASUS", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x1b, 0x90a70130},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1043, "ASUS", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x1a, 0x90a70130},
		{0x1b, 0x90170110},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x14, 0x901701a0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x14, 0x901701b0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60150},
		{0x14, 0x901701a0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60150},
		{0x14, 0x901701b0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x1b, 0x90170110}),
	SND_HDA_PIN_QUIRK(0x10ec0233, 0x8086, "Intel NUC Skull Canyon", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x1b, 0x01111010},
		{0x1e, 0x01451130},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0235, 0x17aa, "Lenovo", ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY,
		{0x12, 0x90a60140},
		{0x14, 0x90170110},
		{0x19, 0x02a11030},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0235, 0x17aa, "Lenovo", ALC294_FIXUP_LENOVO_MIC_LOCATION,
		{0x14, 0x90170110},
		{0x19, 0x02a11030},
		{0x1a, 0x02a11040},
		{0x1b, 0x01014020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0235, 0x17aa, "Lenovo", ALC294_FIXUP_LENOVO_MIC_LOCATION,
		{0x14, 0x90170110},
		{0x19, 0x02a11030},
		{0x1a, 0x02a11040},
		{0x1b, 0x01011020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0235, 0x17aa, "Lenovo", ALC294_FIXUP_LENOVO_MIC_LOCATION,
		{0x14, 0x90170110},
		{0x19, 0x02a11020},
		{0x1a, 0x02a11030},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL2_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60140},
		{0x14, 0x90170110},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x1b, 0x02011020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x1b, 0x01011020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x1b, 0x01014020},
		{0x21, 0x0221103f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x1b, 0x01011020},
		{0x21, 0x0221103f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x1b, 0x02011020},
		{0x21, 0x0221103f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170150},
		{0x1b, 0x02011020},
		{0x21, 0x0221105f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x1b, 0x01014020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170120},
		{0x17, 0x90170140},
		{0x21, 0x0321102f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170140},
		{0x21, 0x02211050}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90171130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90170140},
		{0x21, 0x02211050}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell Inspiron 5548", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60180},
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell Inspiron 5565", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60180},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x1b, 0x01011020},
		{0x21, 0x02211010}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1043, "ASUS", ALC256_FIXUP_ASUS_MIC,
		{0x14, 0x90170110},
		{0x1b, 0x90a70130},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1043, "ASUS", ALC256_FIXUP_ASUS_MIC,
		{0x14, 0x90170110},
		{0x1b, 0x90a70130},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1043, "ASUS", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1043, "ASUS", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1043, "ASUS", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x1a, 0x90a70130},
		{0x1b, 0x90170110},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0280, 0x103c, "HP", ALC280_FIXUP_HP_GPIO4,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x15, 0x0421101f},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0280, 0x103c, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED,
		{0x12, 0x90a60140},
		{0x14, 0x90170110},
		{0x15, 0x0421101f},
		{0x18, 0x02811030},
		{0x1a, 0x04a1103f},
		{0x1b, 0x02011020}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP 15 Touchsmart", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x03a11020},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x03a11020},
		{0x21, 0x03211040}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x03a11030},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x04a11020},
		{0x21, 0x0421101f}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a60140},
		{0x19, 0x04a11030},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0283, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a60130},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0283, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0283, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a60130},
		{0x19, 0x03a11020},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0285, 0x17aa, "Lenovo", ALC285_FIXUP_LENOVO_PC_BEEP_IN_NOISE,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x19, 0x04a11040},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0286, 0x1025, "Acer", ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE,
		{0x12, 0x90a60130},
		{0x17, 0x90170110},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0288, 0x1028, "Dell", ALC288_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60120},
		{0x14, 0x90170110},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x04211040},
		{0x18, 0x90170112},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x04211040},
		{0x18, 0x90170110},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x0421101f},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x04211020},
		{0x1a, 0x04a11040}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x14, 0x90170110},
		{0x15, 0x04211020},
		{0x1a, 0x04a11040}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x14, 0x90170110},
		{0x15, 0x04211020},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x14, 0x90170110},
		{0x15, 0x0421101f},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0292, 0x1028, "Dell", ALC269_FIXUP_DELL2_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x12, 0x90a60140},
		{0x16, 0x01014020},
		{0x19, 0x01a19030}),
	SND_HDA_PIN_QUIRK(0x10ec0292, 0x1028, "Dell", ALC269_FIXUP_DELL2_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x12, 0x90a60140},
		{0x16, 0x01014020},
		{0x18, 0x02a19031},
		{0x19, 0x01a1903e}),
	SND_HDA_PIN_QUIRK(0x10ec0292, 0x1028, "Dell", ALC269_FIXUP_DELL3_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x12, 0x90a60140}),
	SND_HDA_PIN_QUIRK(0x10ec0293, 0x1028, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x13, 0x90a60140},
		{0x16, 0x21014020},
		{0x19, 0x21a19030}),
	SND_HDA_PIN_QUIRK(0x10ec0293, 0x1028, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x13, 0x90a60140}),
	SND_HDA_PIN_QUIRK(0x10ec0294, 0x1043, "ASUS", ALC294_FIXUP_ASUS_MIC,
		{0x14, 0x90170110},
		{0x1b, 0x90a70130},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0294, 0x1043, "ASUS", ALC294_FIXUP_ASUS_SPK,
		{0x12, 0x90a60130},
		{0x17, 0x90170110},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0294, 0x1043, "ASUS", ALC294_FIXUP_ASUS_SPK,
		{0x12, 0x90a60130},
		{0x17, 0x90170110},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1043, "ASUS", ALC294_FIXUP_ASUS_SPK,
		{0x12, 0x90a60130},
		{0x17, 0x90170110},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1043, "ASUS", ALC295_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x12, 0x90a60120},
		{0x17, 0x90170110},
		{0x21, 0x04211030}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1043, "ASUS", ALC295_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x12, 0x90a60130},
		{0x17, 0x90170110},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1043, "ASUS", ALC295_FIXUP_ASUS_MIC_NO_PRESENCE,
		{0x12, 0x90a60130},
		{0x17, 0x90170110},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x21, 0x04211030}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC295_STANDARD_PINS,
		{0x17, 0x21014020},
		{0x18, 0x21a19030}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC295_STANDARD_PINS,
		{0x17, 0x21014040},
		{0x18, 0x21a19050}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC295_STANDARD_PINS),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC298_STANDARD_PINS,
		{0x17, 0x90170110}),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC298_STANDARD_PINS,
		{0x17, 0x90170140}),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC298_STANDARD_PINS,
		{0x17, 0x90170150}),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_SPK_VOLUME,
		{0x12, 0xb7a60140},
		{0x13, 0xb7a60150},
		{0x17, 0x90170110},
		{0x1a, 0x03011020},
		{0x21, 0x03211030}),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_ALIENWARE_MIC_NO_PRESENCE,
		{0x12, 0xb7a60140},
		{0x17, 0x90170110},
		{0x1a, 0x03a11030},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0299, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x17, 0x90170110}),
	{}
};

/* This is the fallback pin_fixup_tbl for alc269 family, to make the tbl match
 * more machines, don't need to match all valid pins, just need to match
 * all the pins defined in the tbl. Just because of this reason, it is possible
 * that a single machine matches multiple tbls, so there is one limitation:
 *   at most one tbl is allowed to define for the same vendor and same codec
 */
static const struct snd_hda_pin_quirk alc269_fallback_pin_fixup_tbl[] = {
	SND_HDA_PIN_QUIRK(0x10ec0289, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
		{0x19, 0x40000000},
		{0x1b, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x19, 0x40000000},
		{0x1a, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0236, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x19, 0x40000000},
		{0x1a, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0274, 0x1028, "Dell", ALC274_FIXUP_DELL_AIO_LINEOUT_VERB,
		{0x19, 0x40000000},
		{0x1a, 0x40000000}),
	{}
};

static void alc269_fill_coef(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int val;

	if (spec->codec_variant != ALC269_TYPE_ALC269VB)
		return;

	if ((alc_get_coef0(codec) & 0x00ff) < 0x015) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8817);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x016) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8814);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x017) {
		/* Power up output pin */
		alc_update_coef_idx(codec, 0x04, 0, 1<<11);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x018) {
		val = alc_read_coef_idx(codec, 0xd);
		if (val != -1 && (val & 0x0c00) >> 10 != 0x1) {
			/* Capless ramp up clock control */
			alc_write_coef_idx(codec, 0xd, val | (1<<10));
		}
		val = alc_read_coef_idx(codec, 0x17);
		if (val != -1 && (val & 0x01c0) >> 6 != 0x4) {
			/* Class D power on reset */
			alc_write_coef_idx(codec, 0x17, val | (1<<7));
		}
	}

	/* HP */
	alc_update_coef_idx(codec, 0x4, 0, 1<<11);
}

/*
 */
static int patch_alc269(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.shared_mic_vref_pin = 0x18;
	codec->power_save_node = 0;

#ifdef CONFIG_PM
	codec->patch_ops.suspend = alc269_suspend;
	codec->patch_ops.resume = alc269_resume;
#endif
	spec->shutup = alc_default_shutup;
	spec->init_hook = alc_default_init;

	switch (codec->core.vendor_id) {
	case 0x10ec0269:
		spec->codec_variant = ALC269_TYPE_ALC269VA;
		switch (alc_get_coef0(codec) & 0x00f0) {
		case 0x0010:
			if (codec->bus->pci &&
			    codec->bus->pci->subsystem_vendor == 0x1025 &&
			    spec->cdefine.platform_type == 1)
				err = alc_codec_rename(codec, "ALC271X");
			spec->codec_variant = ALC269_TYPE_ALC269VB;
			break;
		case 0x0020:
			if (codec->bus->pci &&
			    codec->bus->pci->subsystem_vendor == 0x17aa &&
			    codec->bus->pci->subsystem_device == 0x21f3)
				err = alc_codec_rename(codec, "ALC3202");
			spec->codec_variant = ALC269_TYPE_ALC269VC;
			break;
		case 0x0030:
			spec->codec_variant = ALC269_TYPE_ALC269VD;
			break;
		default:
			alc_fix_pll_init(codec, 0x20, 0x04, 15);
		}
		if (err < 0)
			goto error;
		spec->shutup = alc269_shutup;
		spec->init_hook = alc269_fill_coef;
		alc269_fill_coef(codec);
		break;

	case 0x10ec0280:
	case 0x10ec0290:
		spec->codec_variant = ALC269_TYPE_ALC280;
		break;
	case 0x10ec0282:
		spec->codec_variant = ALC269_TYPE_ALC282;
		spec->shutup = alc282_shutup;
		spec->init_hook = alc282_init;
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		spec->codec_variant = ALC269_TYPE_ALC283;
		spec->shutup = alc283_shutup;
		spec->init_hook = alc283_init;
		break;
	case 0x10ec0284:
	case 0x10ec0292:
		spec->codec_variant = ALC269_TYPE_ALC284;
		break;
	case 0x10ec0293:
		spec->codec_variant = ALC269_TYPE_ALC293;
		break;
	case 0x10ec0286:
	case 0x10ec0288:
		spec->codec_variant = ALC269_TYPE_ALC286;
		break;
	case 0x10ec0298:
		spec->codec_variant = ALC269_TYPE_ALC298;
		break;
	case 0x10ec0235:
	case 0x10ec0255:
		spec->codec_variant = ALC269_TYPE_ALC255;
		spec->shutup = alc256_shutup;
		spec->init_hook = alc256_init;
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		spec->codec_variant = ALC269_TYPE_ALC256;
		spec->shutup = alc256_shutup;
		spec->init_hook = alc256_init;
		spec->gen.mixer_nid = 0; /* ALC256 does not have any loopback mixer path */
		break;
	case 0x10ec0257:
		spec->codec_variant = ALC269_TYPE_ALC257;
		spec->shutup = alc256_shutup;
		spec->init_hook = alc256_init;
		spec->gen.mixer_nid = 0;
		break;
	case 0x10ec0215:
	case 0x10ec0245:
	case 0x10ec0285:
	case 0x10ec0287:
	case 0x10ec0289:
		spec->codec_variant = ALC269_TYPE_ALC215;
		spec->shutup = alc225_shutup;
		spec->init_hook = alc225_init;
		spec->gen.mixer_nid = 0;
		break;
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		spec->codec_variant = ALC269_TYPE_ALC225;
		spec->shutup = alc225_shutup;
		spec->init_hook = alc225_init;
		spec->gen.mixer_nid = 0; /* no loopback on ALC225, ALC295 and ALC299 */
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		spec->codec_variant = ALC269_TYPE_ALC294;
		spec->gen.mixer_nid = 0; /* ALC2x4 does not have any loopback mixer path */
		alc_update_coef_idx(codec, 0x6b, 0x0018, (1<<4) | (1<<3)); /* UAJ MIC Vref control by verb */
		spec->init_hook = alc294_init;
		break;
	case 0x10ec0300:
		spec->codec_variant = ALC269_TYPE_ALC300;
		spec->gen.mixer_nid = 0; /* no loopback on ALC300 */
		break;
	case 0x10ec0623:
		spec->codec_variant = ALC269_TYPE_ALC623;
		break;
	case 0x10ec0700:
	case 0x10ec0701:
	case 0x10ec0703:
	case 0x10ec0711:
		spec->codec_variant = ALC269_TYPE_ALC700;
		spec->gen.mixer_nid = 0; /* ALC700 does not have any loopback mixer path */
		alc_update_coef_idx(codec, 0x4a, 1 << 15, 0); /* Combo jack auto trigger control */
		spec->init_hook = alc294_init;
		break;

	}

	if (snd_hda_codec_read(codec, 0x51, 0, AC_VERB_PARAMETERS, 0) == 0x10ec5505) {
		spec->has_alc5505_dsp = 1;
		spec->init_hook = alc5505_dsp_init;
	}

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc269_fixup_models,
		       alc269_fixup_tbl, alc269_fixups);
	snd_hda_pick_pin_fixup(codec, alc269_pin_fixup_tbl, alc269_fixups, true);
	snd_hda_pick_pin_fixup(codec, alc269_fallback_pin_fixup_tbl, alc269_fixups, false);
	snd_hda_pick_fixup(codec, NULL,	alc269_fixup_vendor_tbl,
			   alc269_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	/* automatic parse from the BIOS config */
	err = alc269_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid && spec->gen.mixer_nid) {
		err = set_beep_amp(spec, spec->gen.mixer_nid, 0x04, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC861
 */

static int alc861_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861_ssids[] = { 0x0e, 0x0f, 0x0b, 0 };
	return alc_parse_auto_config(codec, alc861_ignore, alc861_ssids);
}

/* Pin config fixes */
enum {
	ALC861_FIXUP_FSC_AMILO_PI1505,
	ALC861_FIXUP_AMP_VREF_0F,
	ALC861_FIXUP_NO_JACK_DETECT,
	ALC861_FIXUP_ASUS_A6RP,
	ALC660_FIXUP_ASUS_W7J,
};

/* On some laptops, VREF of pin 0x0f is abused for controlling the main amp */
static void alc861_fixup_asus_amp_vref_0f(struct hda_codec *codec,
			const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val;

	if (action != HDA_FIXUP_ACT_INIT)
		return;
	val = snd_hda_codec_get_pin_target(codec, 0x0f);
	if (!(val & (AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN)))
		val |= AC_PINCTL_IN_EN;
	val |= AC_PINCTL_VREF_50;
	snd_hda_set_pin_ctl(codec, 0x0f, val);
	spec->gen.keep_vref_in_automute = 1;
}

/* suppress the jack-detection */
static void alc_fixup_no_jack_detect(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		codec->no_jack_detect = 1;
}

static const struct hda_fixup alc861_fixups[] = {
	[ALC861_FIXUP_FSC_AMILO_PI1505] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x0b, 0x0221101f }, /* HP */
			{ 0x0f, 0x90170310 }, /* speaker */
			{ }
		}
	},
	[ALC861_FIXUP_AMP_VREF_0F] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc861_fixup_asus_amp_vref_0f,
	},
	[ALC861_FIXUP_NO_JACK_DETECT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_jack_detect,
	},
	[ALC861_FIXUP_ASUS_A6RP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc861_fixup_asus_amp_vref_0f,
		.chained = true,
		.chain_id = ALC861_FIXUP_NO_JACK_DETECT,
	},
	[ALC660_FIXUP_ASUS_W7J] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* ASUS W7J needs a magic pin setup on unused NID 0x10
			 * for enabling outputs
			 */
			{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
			{ }
		},
	}
};

static const struct snd_pci_quirk alc861_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1253, "ASUS W7J", ALC660_FIXUP_ASUS_W7J),
	SND_PCI_QUIRK(0x1043, 0x1263, "ASUS Z35HL", ALC660_FIXUP_ASUS_W7J),
	SND_PCI_QUIRK(0x1043, 0x1393, "ASUS A6Rp", ALC861_FIXUP_ASUS_A6RP),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS laptop", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1462, 0x7254, "HP DX2200", ALC861_FIXUP_NO_JACK_DETECT),
	SND_PCI_QUIRK(0x1584, 0x2b01, "Haier W18", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1584, 0x0000, "Uniwill ECS M31EI", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1734, 0x10c7, "FSC Amilo Pi1505", ALC861_FIXUP_FSC_AMILO_PI1505),
	{}
};

/*
 */
static int patch_alc861(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x15);
	if (err < 0)
		return err;

	spec = codec->spec;
	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x23;

#ifdef CONFIG_PM
	spec->power_hook = alc_power_eapd;
#endif

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, NULL, alc861_fixup_tbl, alc861_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc861_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog) {
		err = set_beep_amp(spec, 0x23, 0, HDA_OUTPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC861-VD support
 *
 * Based on ALC882
 *
 * In addition, an independent DAC
 */
static int alc861vd_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861vd_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc861vd_ignore, alc861vd_ssids);
}

enum {
	ALC660VD_FIX_ASUS_GPIO1,
	ALC861VD_FIX_DALLAS,
};

/* exclude VREF80 */
static void alc861vd_fixup_dallas(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		snd_hda_override_pin_caps(codec, 0x18, 0x00000734);
		snd_hda_override_pin_caps(codec, 0x19, 0x0000073c);
	}
}

/* reset GPIO1 */
static void alc660vd_fixup_asus_gpio1(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->gpio_mask |= 0x02;
	alc_fixup_gpio(codec, action, 0x01);
}

static const struct hda_fixup alc861vd_fixups[] = {
	[ALC660VD_FIX_ASUS_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc660vd_fixup_asus_gpio1,
	},
	[ALC861VD_FIX_DALLAS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc861vd_fixup_dallas,
	},
};

static const struct snd_pci_quirk alc861vd_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30bf, "HP TX1000", ALC861VD_FIX_DALLAS),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS A7-K", ALC660VD_FIX_ASUS_GPIO1),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba L30-149", ALC861VD_FIX_DALLAS),
	{}
};

/*
 */
static int patch_alc861vd(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x23;

	spec->shutup = alc_eapd_shutup;

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, NULL, alc861vd_fixup_tbl, alc861vd_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc861vd_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog) {
		err = set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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

/*
 * BIOS auto configuration
 */

static int alc662_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc662_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc663_ssids[] = { 0x15, 0x1b, 0x14, 0x21 };
	static const hda_nid_t alc662_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	const hda_nid_t *ssids;

	if (codec->core.vendor_id == 0x10ec0272 || codec->core.vendor_id == 0x10ec0663 ||
	    codec->core.vendor_id == 0x10ec0665 || codec->core.vendor_id == 0x10ec0670 ||
	    codec->core.vendor_id == 0x10ec0671)
		ssids = alc663_ssids;
	else
		ssids = alc662_ssids;
	return alc_parse_auto_config(codec, alc662_ignore, ssids);
}

static void alc272_fixup_mario(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	if (snd_hda_override_amp_caps(codec, 0x2, HDA_OUTPUT,
				      (0x3b << AC_AMPCAP_OFFSET_SHIFT) |
				      (0x3b << AC_AMPCAP_NUM_STEPS_SHIFT) |
				      (0x03 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				      (0 << AC_AMPCAP_MUTE_SHIFT)))
		codec_warn(codec, "failed to override amp caps for NID 0x2\n");
}

static const struct snd_pcm_chmap_elem asus_pcm_2_1_chmaps[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 4,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_LFE } }, /* LFE only on right */
	{ }
};

/* override the 2.1 chmap */
static void alc_fixup_bass_chmap(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_BUILD) {
		struct alc_spec *spec = codec->spec;
		spec->gen.pcm_rec[0]->stream[0].chmap = asus_pcm_2_1_chmaps;
	}
}

/* avoid D3 for keeping GPIO up */
static unsigned int gpio_led_power_filter(struct hda_codec *codec,
					  hda_nid_t nid,
					  unsigned int power_state)
{
	struct alc_spec *spec = codec->spec;
	if (nid == codec->core.afg && power_state == AC_PWRST_D3 && spec->gpio_data)
		return AC_PWRST_D0;
	return power_state;
}

static void alc662_fixup_led_gpio1(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	alc_fixup_hp_gpio_led(codec, action, 0x01, 0);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 1;
		codec->power_filter = gpio_led_power_filter;
	}
}

static void alc662_usi_automute_hook(struct hda_codec *codec,
					 struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	int vref;
	msleep(200);
	snd_hda_gen_hp_automute(codec, jack);

	vref = spec->gen.hp_jack_present ? PIN_VREF80 : 0;
	msleep(100);
	snd_hda_codec_write(codec, 0x19, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    vref);
}

static void alc662_fixup_usi_headset_mic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		spec->gen.hp_automute_hook = alc662_usi_automute_hook;
	}
}

static void alc662_aspire_ethos_mute_speakers(struct hda_codec *codec,
					struct hda_jack_callback *cb)
{
	/* surround speakers at 0x1b already get muted automatically when
	 * headphones are plugged in, but we have to mute/unmute the remaining
	 * channels manually:
	 * 0x15 - front left/front right
	 * 0x18 - front center/ LFE
	 */
	if (snd_hda_jack_detect_state(codec, 0x1b) == HDA_JACK_PRESENT) {
		snd_hda_set_pin_ctl_cache(codec, 0x15, 0);
		snd_hda_set_pin_ctl_cache(codec, 0x18, 0);
	} else {
		snd_hda_set_pin_ctl_cache(codec, 0x15, PIN_OUT);
		snd_hda_set_pin_ctl_cache(codec, 0x18, PIN_OUT);
	}
}

static void alc662_fixup_aspire_ethos_hp(struct hda_codec *codec,
					const struct hda_fixup *fix, int action)
{
    /* Pin 0x1b: shared headphones jack and surround speakers */
	if (!is_jack_detectable(codec, 0x1b))
		return;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_jack_detect_enable_callback(codec, 0x1b,
				alc662_aspire_ethos_mute_speakers);
		/* subwoofer needs an extra GPIO setting to become audible */
		alc_setup_gpio(codec, 0x02);
		break;
	case HDA_FIXUP_ACT_INIT:
		/* Make sure to start in a correct state, i.e. if
		 * headphones have been plugged in before powering up the system
		 */
		alc662_aspire_ethos_mute_speakers(codec, NULL);
		break;
	}
}

static void alc671_fixup_hp_headset_mic2(struct hda_codec *codec,
					     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	static const struct hda_pintbl pincfgs[] = {
		{ 0x19, 0x02a11040 }, /* use as headset mic, with its own jack detect */
		{ 0x1b, 0x0181304f },
		{ }
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gen.mixer_nid = 0;
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		snd_hda_apply_pincfgs(codec, pincfgs);
		break;
	case HDA_FIXUP_ACT_INIT:
		alc_write_coef_idx(codec, 0x19, 0xa054);
		break;
	}
}

static const struct coef_fw alc668_coefs[] = {
	WRITE_COEF(0x01, 0xbebe), WRITE_COEF(0x02, 0xaaaa), WRITE_COEF(0x03,    0x0),
	WRITE_COEF(0x04, 0x0180), WRITE_COEF(0x06,    0x0), WRITE_COEF(0x07, 0x0f80),
	WRITE_COEF(0x08, 0x0031), WRITE_COEF(0x0a, 0x0060), WRITE_COEF(0x0b,    0x0),
	WRITE_COEF(0x0c, 0x7cf7), WRITE_COEF(0x0d, 0x1080), WRITE_COEF(0x0e, 0x7f7f),
	WRITE_COEF(0x0f, 0xcccc), WRITE_COEF(0x10, 0xddcc), WRITE_COEF(0x11, 0x0001),
	WRITE_COEF(0x13,    0x0), WRITE_COEF(0x14, 0x2aa0), WRITE_COEF(0x17, 0xa940),
	WRITE_COEF(0x19,    0x0), WRITE_COEF(0x1a,    0x0), WRITE_COEF(0x1b,    0x0),
	WRITE_COEF(0x1c,    0x0), WRITE_COEF(0x1d,    0x0), WRITE_COEF(0x1e, 0x7418),
	WRITE_COEF(0x1f, 0x0804), WRITE_COEF(0x20, 0x4200), WRITE_COEF(0x21, 0x0468),
	WRITE_COEF(0x22, 0x8ccc), WRITE_COEF(0x23, 0x0250), WRITE_COEF(0x24, 0x7418),
	WRITE_COEF(0x27,    0x0), WRITE_COEF(0x28, 0x8ccc), WRITE_COEF(0x2a, 0xff00),
	WRITE_COEF(0x2b, 0x8000), WRITE_COEF(0xa7, 0xff00), WRITE_COEF(0xa8, 0x8000),
	WRITE_COEF(0xaa, 0x2e17), WRITE_COEF(0xab, 0xa0c0), WRITE_COEF(0xac,    0x0),
	WRITE_COEF(0xad,    0x0), WRITE_COEF(0xae, 0x2ac6), WRITE_COEF(0xaf, 0xa480),
	WRITE_COEF(0xb0,    0x0), WRITE_COEF(0xb1,    0x0), WRITE_COEF(0xb2,    0x0),
	WRITE_COEF(0xb3,    0x0), WRITE_COEF(0xb4,    0x0), WRITE_COEF(0xb5, 0x1040),
	WRITE_COEF(0xb6, 0xd697), WRITE_COEF(0xb7, 0x902b), WRITE_COEF(0xb8, 0xd697),
	WRITE_COEF(0xb9, 0x902b), WRITE_COEF(0xba, 0xb8ba), WRITE_COEF(0xbb, 0xaaab),
	WRITE_COEF(0xbc, 0xaaaf), WRITE_COEF(0xbd, 0x6aaa), WRITE_COEF(0xbe, 0x1c02),
	WRITE_COEF(0xc0, 0x00ff), WRITE_COEF(0xc1, 0x0fa6),
	{}
};

static void alc668_restore_default_value(struct hda_codec *codec)
{
	alc_process_coef_fw(codec, alc668_coefs);
}

enum {
	ALC662_FIXUP_ASPIRE,
	ALC662_FIXUP_LED_GPIO1,
	ALC662_FIXUP_IDEAPAD,
	ALC272_FIXUP_MARIO,
	ALC662_FIXUP_CZC_P10T,
	ALC662_FIXUP_SKU_IGNORE,
	ALC662_FIXUP_HP_RP5800,
	ALC662_FIXUP_ASUS_MODE1,
	ALC662_FIXUP_ASUS_MODE2,
	ALC662_FIXUP_ASUS_MODE3,
	ALC662_FIXUP_ASUS_MODE4,
	ALC662_FIXUP_ASUS_MODE5,
	ALC662_FIXUP_ASUS_MODE6,
	ALC662_FIXUP_ASUS_MODE7,
	ALC662_FIXUP_ASUS_MODE8,
	ALC662_FIXUP_NO_JACK_DETECT,
	ALC662_FIXUP_ZOTAC_Z68,
	ALC662_FIXUP_INV_DMIC,
	ALC662_FIXUP_DELL_MIC_NO_PRESENCE,
	ALC668_FIXUP_DELL_MIC_NO_PRESENCE,
	ALC662_FIXUP_HEADSET_MODE,
	ALC668_FIXUP_HEADSET_MODE,
	ALC662_FIXUP_BASS_MODE4_CHMAP,
	ALC662_FIXUP_BASS_16,
	ALC662_FIXUP_BASS_1A,
	ALC662_FIXUP_BASS_CHMAP,
	ALC668_FIXUP_AUTO_MUTE,
	ALC668_FIXUP_DELL_DISABLE_AAMIX,
	ALC668_FIXUP_DELL_XPS13,
	ALC662_FIXUP_ASUS_Nx50,
	ALC668_FIXUP_ASUS_Nx51_HEADSET_MODE,
	ALC668_FIXUP_ASUS_Nx51,
	ALC668_FIXUP_MIC_COEF,
	ALC668_FIXUP_ASUS_G751,
	ALC891_FIXUP_HEADSET_MODE,
	ALC891_FIXUP_DELL_MIC_NO_PRESENCE,
	ALC662_FIXUP_ACER_VERITON,
	ALC892_FIXUP_ASROCK_MOBO,
	ALC662_FIXUP_USI_FUNC,
	ALC662_FIXUP_USI_HEADSET_MODE,
	ALC662_FIXUP_LENOVO_MULTI_CODECS,
	ALC669_FIXUP_ACER_ASPIRE_ETHOS,
	ALC669_FIXUP_ACER_ASPIRE_ETHOS_HEADSET,
	ALC671_FIXUP_HP_HEADSET_MIC2,
	ALC662_FIXUP_ACER_X2660G_HEADSET_MODE,
	ALC662_FIXUP_ACER_NITRO_HEADSET_MODE,
};

static const struct hda_fixup alc662_fixups[] = {
	[ALC662_FIXUP_ASPIRE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x99130112 }, /* subwoofer */
			{ }
		}
	},
	[ALC662_FIXUP_LED_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc662_fixup_led_gpio1,
	},
	[ALC662_FIXUP_IDEAPAD] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x99130112 }, /* subwoofer */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_LED_GPIO1,
	},
	[ALC272_FIXUP_MARIO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc272_fixup_mario,
	},
	[ALC662_FIXUP_CZC_P10T] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC662_FIXUP_SKU_IGNORE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_sku_ignore,
	},
	[ALC662_FIXUP_HP_RP5800] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0221201f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE1] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19820 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x1b, 0x0121401f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE3] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121441f }, /* HP */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x21, 0x01211420 }, /* HP2 */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE4] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x16, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x21, 0x0121441f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE5] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121441f }, /* HP */
			{ 0x16, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE6] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x01211420 }, /* HP2 */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x1b, 0x0121441f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE7] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x17, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x1b, 0x01214020 }, /* HP */
			{ 0x21, 0x0121401f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE8] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x12, 0x99a30970 }, /* int-mic */
			{ 0x15, 0x01214020 }, /* HP */
			{ 0x17, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x21, 0x0121401f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_NO_JACK_DETECT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_jack_detect,
	},
	[ALC662_FIXUP_ZOTAC_Z68] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x02214020 }, /* Front HP */
			{ }
		}
	},
	[ALC662_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC668_FIXUP_DELL_XPS13] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell_xps13,
		.chained = true,
		.chain_id = ALC668_FIXUP_DELL_DISABLE_AAMIX
	},
	[ALC668_FIXUP_DELL_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC668_FIXUP_DELL_MIC_NO_PRESENCE
	},
	[ALC668_FIXUP_AUTO_MUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_auto_mute_via_amp,
		.chained = true,
		.chain_id = ALC668_FIXUP_DELL_MIC_NO_PRESENCE
	},
	[ALC662_FIXUP_DELL_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			/* headphone mic by setting pin control of 0x1b (headphone out) to in + vref_50 */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_HEADSET_MODE
	},
	[ALC662_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc662,
	},
	[ALC668_FIXUP_DELL_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1913d }, /* use as headphone mic, without its own jack detect */
			{ 0x1b, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC668_FIXUP_HEADSET_MODE
	},
	[ALC668_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc668,
	},
	[ALC662_FIXUP_BASS_MODE4_CHMAP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_bass_chmap,
		.chained = true,
		.chain_id = ALC662_FIXUP_ASUS_MODE4
	},
	[ALC662_FIXUP_BASS_16] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x16, 0x80106111}, /* bass speaker */
			{}
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_BASS_CHMAP,
	},
	[ALC662_FIXUP_BASS_1A] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x1a, 0x80106111}, /* bass speaker */
			{}
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_BASS_CHMAP,
	},
	[ALC662_FIXUP_BASS_CHMAP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_bass_chmap,
	},
	[ALC662_FIXUP_ASUS_Nx50] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_auto_mute_via_amp,
		.chained = true,
		.chain_id = ALC662_FIXUP_BASS_1A
	},
	[ALC668_FIXUP_ASUS_Nx51_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc668,
		.chain_id = ALC662_FIXUP_BASS_CHMAP
	},
	[ALC668_FIXUP_ASUS_Nx51] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1913d }, /* use as headphone mic, without its own jack detect */
			{ 0x1a, 0x90170151 }, /* bass speaker */
			{ 0x1b, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			{}
		},
		.chained = true,
		.chain_id = ALC668_FIXUP_ASUS_Nx51_HEADSET_MODE,
	},
	[ALC668_FIXUP_MIC_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0xc3 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x4000 },
			{}
		},
	},
	[ALC668_FIXUP_ASUS_G751] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x0421101f }, /* HP */
			{}
		},
		.chained = true,
		.chain_id = ALC668_FIXUP_MIC_COEF
	},
	[ALC891_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode,
	},
	[ALC891_FIXUP_DELL_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1913d }, /* use as headphone mic, without its own jack detect */
			{ 0x1b, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC891_FIXUP_HEADSET_MODE
	},
	[ALC662_FIXUP_ACER_VERITON] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x50170120 }, /* no internal speaker */
			{ }
		}
	},
	[ALC892_FIXUP_ASROCK_MOBO] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x40f000f0 }, /* disabled */
			{ 0x16, 0x40f000f0 }, /* disabled */
			{ }
		}
	},
	[ALC662_FIXUP_USI_FUNC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc662_fixup_usi_headset_mic,
	},
	[ALC662_FIXUP_USI_HEADSET_MODE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x02a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x18, 0x01a1903d },
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_USI_FUNC
	},
	[ALC662_FIXUP_LENOVO_MULTI_CODECS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc233_alc662_fixup_lenovo_dual_codecs,
	},
	[ALC669_FIXUP_ACER_ASPIRE_ETHOS_HEADSET] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc662_fixup_aspire_ethos_hp,
	},
	[ALC669_FIXUP_ACER_ASPIRE_ETHOS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x92130110 }, /* front speakers */
			{ 0x18, 0x99130111 }, /* center/subwoofer */
			{ 0x1b, 0x11130012 }, /* surround plus jack for HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC669_FIXUP_ACER_ASPIRE_ETHOS_HEADSET
	},
	[ALC671_FIXUP_HP_HEADSET_MIC2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc671_fixup_hp_headset_mic2,
	},
	[ALC662_FIXUP_ACER_X2660G_HEADSET_MODE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x02a1113c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_USI_FUNC
	},
	[ALC662_FIXUP_ACER_NITRO_HEADSET_MODE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x01a11140 }, /* use as headset mic, without its own jack detect */
			{ 0x1b, 0x0221144f },
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_USI_FUNC
	},
};

static const struct snd_pci_quirk alc662_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x9087, "ECS", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1025, 0x022f, "Acer Aspire One", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0241, "Packard Bell DOTS", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0308, "Acer Aspire 8942G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x031c, "Gateway NV79", ALC662_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x1025, 0x0349, "eMachines eM250", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x034a, "Gateway LT27", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x038b, "Acer Aspire 8943G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x123c, "Acer Nitro N50-600", ALC662_FIXUP_ACER_NITRO_HEADSET_MODE),
	SND_PCI_QUIRK(0x1025, 0x124e, "Acer 2660G", ALC662_FIXUP_ACER_X2660G_HEADSET_MODE),
	SND_PCI_QUIRK(0x1028, 0x05d8, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05db, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05fe, "Dell XPS 15", ALC668_FIXUP_DELL_XPS13),
	SND_PCI_QUIRK(0x1028, 0x060a, "Dell XPS 13", ALC668_FIXUP_DELL_XPS13),
	SND_PCI_QUIRK(0x1028, 0x060d, "Dell M3800", ALC668_FIXUP_DELL_XPS13),
	SND_PCI_QUIRK(0x1028, 0x0625, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0626, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0696, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0698, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x069f, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x1632, "HP RP5800", ALC662_FIXUP_HP_RP5800),
	SND_PCI_QUIRK(0x1043, 0x1080, "Asus UX501VW", ALC668_FIXUP_HEADSET_MODE),
	SND_PCI_QUIRK(0x1043, 0x11cd, "Asus N550", ALC662_FIXUP_ASUS_Nx50),
	SND_PCI_QUIRK(0x1043, 0x13df, "Asus N550JX", ALC662_FIXUP_BASS_1A),
	SND_PCI_QUIRK(0x1043, 0x129d, "Asus N750", ALC662_FIXUP_ASUS_Nx50),
	SND_PCI_QUIRK(0x1043, 0x12ff, "ASUS G751", ALC668_FIXUP_ASUS_G751),
	SND_PCI_QUIRK(0x1043, 0x1477, "ASUS N56VZ", ALC662_FIXUP_BASS_MODE4_CHMAP),
	SND_PCI_QUIRK(0x1043, 0x15a7, "ASUS UX51VZH", ALC662_FIXUP_BASS_16),
	SND_PCI_QUIRK(0x1043, 0x177d, "ASUS N551", ALC668_FIXUP_ASUS_Nx51),
	SND_PCI_QUIRK(0x1043, 0x17bd, "ASUS N751", ALC668_FIXUP_ASUS_Nx51),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71SL", ALC662_FIXUP_ASUS_MODE8),
	SND_PCI_QUIRK(0x1043, 0x1b73, "ASUS N55SF", ALC662_FIXUP_BASS_16),
	SND_PCI_QUIRK(0x1043, 0x1bf3, "ASUS N76VZ", ALC662_FIXUP_BASS_MODE4_CHMAP),
	SND_PCI_QUIRK(0x1043, 0x8469, "ASUS mobo", ALC662_FIXUP_NO_JACK_DETECT),
	SND_PCI_QUIRK(0x105b, 0x0cd6, "Foxconn", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x144d, 0xc051, "Samsung R720", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x14cd, 0x5003, "USI", ALC662_FIXUP_USI_HEADSET_MODE),
	SND_PCI_QUIRK(0x17aa, 0x1036, "Lenovo P520", ALC662_FIXUP_LENOVO_MULTI_CODECS),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo Ideapad Y550P", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Ideapad Y550", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x1849, 0x5892, "ASRock B150M", ALC892_FIXUP_ASROCK_MOBO),
	SND_PCI_QUIRK(0x19da, 0xa130, "Zotac Z68", ALC662_FIXUP_ZOTAC_Z68),
	SND_PCI_QUIRK(0x1b0a, 0x01b8, "ACER Veriton", ALC662_FIXUP_ACER_VERITON),
	SND_PCI_QUIRK(0x1b35, 0x2206, "CZC P10T", ALC662_FIXUP_CZC_P10T),
	SND_PCI_QUIRK(0x1025, 0x0566, "Acer Aspire Ethos 8951G", ALC669_FIXUP_ACER_ASPIRE_ETHOS),

#if 0
	/* Below is a quirk table taken from the old code.
	 * Basically the device should work as is without the fixup table.
	 * If BIOS doesn't give a proper info, enable the corresponding
	 * fixup entry.
	 */
	SND_PCI_QUIRK(0x1043, 0x1000, "ASUS N50Vm", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1092, "ASUS NB", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1173, "ASUS K73Jn", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11c3, "ASUS M70V", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x11d3, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11f3, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1203, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1303, "ASUS G60J", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1333, "ASUS G60Jx", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x13e3, "ASUS N71JA", ALC662_FIXUP_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x1463, "ASUS N71", ALC662_FIXUP_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x14d3, "ASUS G72", ALC662_FIXUP_ASUS_MODE8),
	SND_PCI_QUIRK(0x1043, 0x1563, "ASUS N90", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x15d3, "ASUS N50SF F50SF", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x16c3, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x16f3, "ASUS K40C K50C", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1733, "ASUS N81De", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1753, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1763, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1765, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1783, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1793, "ASUS F50GX", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x17b3, "ASUS F70SL", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x17f3, "ASUS X58LE", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1813, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1823, "ASUS NB", ALC662_FIXUP_ASUS_MODE5),
	SND_PCI_QUIRK(0x1043, 0x1833, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1843, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1853, "ASUS F50Z", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1864, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1876, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1893, "ASUS M50Vm", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1894, "ASUS X55", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x18b3, "ASUS N80Vc", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18c3, "ASUS VX5", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18d3, "ASUS N81Te", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18f3, "ASUS N505Tp", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1903, "ASUS F5GL", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1913, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1933, "ASUS F80Q", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1943, "ASUS Vx3V", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1953, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71C", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1983, "ASUS N5051A", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1993, "ASUS N20", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS F7Z", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19c3, "ASUS F5Z/F6x", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x19e3, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19f3, "ASUS NB", ALC662_FIXUP_ASUS_MODE4),
#endif
	{}
};

static const struct hda_model_fixup alc662_fixup_models[] = {
	{.id = ALC662_FIXUP_ASPIRE, .name = "aspire"},
	{.id = ALC662_FIXUP_IDEAPAD, .name = "ideapad"},
	{.id = ALC272_FIXUP_MARIO, .name = "mario"},
	{.id = ALC662_FIXUP_HP_RP5800, .name = "hp-rp5800"},
	{.id = ALC662_FIXUP_ASUS_MODE1, .name = "asus-mode1"},
	{.id = ALC662_FIXUP_ASUS_MODE2, .name = "asus-mode2"},
	{.id = ALC662_FIXUP_ASUS_MODE3, .name = "asus-mode3"},
	{.id = ALC662_FIXUP_ASUS_MODE4, .name = "asus-mode4"},
	{.id = ALC662_FIXUP_ASUS_MODE5, .name = "asus-mode5"},
	{.id = ALC662_FIXUP_ASUS_MODE6, .name = "asus-mode6"},
	{.id = ALC662_FIXUP_ASUS_MODE7, .name = "asus-mode7"},
	{.id = ALC662_FIXUP_ASUS_MODE8, .name = "asus-mode8"},
	{.id = ALC662_FIXUP_ZOTAC_Z68, .name = "zotac-z68"},
	{.id = ALC662_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC662_FIXUP_DELL_MIC_NO_PRESENCE, .name = "alc662-headset-multi"},
	{.id = ALC668_FIXUP_DELL_MIC_NO_PRESENCE, .name = "dell-headset-multi"},
	{.id = ALC662_FIXUP_HEADSET_MODE, .name = "alc662-headset"},
	{.id = ALC668_FIXUP_HEADSET_MODE, .name = "alc668-headset"},
	{.id = ALC662_FIXUP_BASS_16, .name = "bass16"},
	{.id = ALC662_FIXUP_BASS_1A, .name = "bass1a"},
	{.id = ALC668_FIXUP_AUTO_MUTE, .name = "automute"},
	{.id = ALC668_FIXUP_DELL_XPS13, .name = "dell-xps13"},
	{.id = ALC662_FIXUP_ASUS_Nx50, .name = "asus-nx50"},
	{.id = ALC668_FIXUP_ASUS_Nx51, .name = "asus-nx51"},
	{.id = ALC668_FIXUP_ASUS_G751, .name = "asus-g751"},
	{.id = ALC891_FIXUP_HEADSET_MODE, .name = "alc891-headset"},
	{.id = ALC891_FIXUP_DELL_MIC_NO_PRESENCE, .name = "alc891-headset-multi"},
	{.id = ALC662_FIXUP_ACER_VERITON, .name = "acer-veriton"},
	{.id = ALC892_FIXUP_ASROCK_MOBO, .name = "asrock-mobo"},
	{.id = ALC662_FIXUP_USI_HEADSET_MODE, .name = "usi-headset"},
	{.id = ALC662_FIXUP_LENOVO_MULTI_CODECS, .name = "dual-codecs"},
	{.id = ALC669_FIXUP_ACER_ASPIRE_ETHOS, .name = "aspire-ethos"},
	{}
};

static const struct snd_hda_pin_quirk alc662_pin_fixup_tbl[] = {
	SND_HDA_PIN_QUIRK(0x10ec0867, 0x1028, "Dell", ALC891_FIXUP_DELL_MIC_NO_PRESENCE,
		{0x17, 0x02211010},
		{0x18, 0x01a19030},
		{0x1a, 0x01813040},
		{0x21, 0x01014020}),
	SND_HDA_PIN_QUIRK(0x10ec0867, 0x1028, "Dell", ALC891_FIXUP_DELL_MIC_NO_PRESENCE,
		{0x16, 0x01813030},
		{0x17, 0x02211010},
		{0x18, 0x01a19040},
		{0x21, 0x01014020}),
	SND_HDA_PIN_QUIRK(0x10ec0662, 0x1028, "Dell", ALC662_FIXUP_DELL_MIC_NO_PRESENCE,
		{0x14, 0x01014010},
		{0x18, 0x01a19020},
		{0x1a, 0x0181302f},
		{0x1b, 0x0221401f}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x99a30130},
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x99a30140},
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x99a30150},
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell XPS 15", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x15, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0671, 0x103c, "HP cPC", ALC671_FIXUP_HP_HEADSET_MIC2,
		{0x14, 0x01014010},
		{0x17, 0x90170150},
		{0x19, 0x02a11060},
		{0x1b, 0x01813030},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0671, 0x103c, "HP cPC", ALC671_FIXUP_HP_HEADSET_MIC2,
		{0x14, 0x01014010},
		{0x18, 0x01a19040},
		{0x1b, 0x01813030},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0671, 0x103c, "HP cPC", ALC671_FIXUP_HP_HEADSET_MIC2,
		{0x14, 0x01014020},
		{0x17, 0x90170110},
		{0x18, 0x01a19050},
		{0x1b, 0x01813040},
		{0x21, 0x02211030}),
	{}
};

/*
 */
static int patch_alc662(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;

	spec->shutup = alc_eapd_shutup;

	/* handle multiple HPs as is */
	spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	switch (codec->core.vendor_id) {
	case 0x10ec0668:
		spec->init_hook = alc668_restore_default_value;
		break;
	}

	alc_pre_init(codec);

	snd_hda_pick_fixup(codec, alc662_fixup_models,
		       alc662_fixup_tbl, alc662_fixups);
	snd_hda_pick_pin_fixup(codec, alc662_pin_fixup_tbl, alc662_fixups, true);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	if ((alc_get_coef0(codec) & (1 << 14)) &&
	    codec->bus->pci && codec->bus->pci->subsystem_vendor == 0x1025 &&
	    spec->cdefine.platform_type == 1) {
		err = alc_codec_rename(codec, "ALC272X");
		if (err < 0)
			goto error;
	}

	/* automatic parse from the BIOS config */
	err = alc662_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid) {
		switch (codec->core.vendor_id) {
		case 0x10ec0662:
			err = set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
			break;
		case 0x10ec0272:
		case 0x10ec0663:
		case 0x10ec0665:
		case 0x10ec0668:
			err = set_beep_amp(spec, 0x0b, 0x04, HDA_INPUT);
			break;
		case 0x10ec0273:
			err = set_beep_amp(spec, 0x0b, 0x03, HDA_INPUT);
			break;
		}
		if (err < 0)
			goto error;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC680 support
 */

static int alc680_parse_auto_config(struct hda_codec *codec)
{
	return alc_parse_auto_config(codec, NULL, NULL);
}

/*
 */
static int patch_alc680(struct hda_codec *codec)
{
	int err;

	/* ALC680 has no aa-loopback mixer */
	err = alc_alloc_spec(codec, 0);
	if (err < 0)
		return err;

	/* automatic parse from the BIOS config */
	err = alc680_parse_auto_config(codec);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	return 0;
}

/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_realtek[] = {
	HDA_CODEC_ENTRY(0x10ec0215, "ALC215", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0221, "ALC221", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0222, "ALC222", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0225, "ALC225", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0231, "ALC231", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0233, "ALC233", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0234, "ALC234", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0235, "ALC233", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0236, "ALC236", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0245, "ALC245", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0255, "ALC255", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0256, "ALC256", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0257, "ALC257", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0260, "ALC260", patch_alc260),
	HDA_CODEC_ENTRY(0x10ec0262, "ALC262", patch_alc262),
	HDA_CODEC_ENTRY(0x10ec0267, "ALC267", patch_alc268),
	HDA_CODEC_ENTRY(0x10ec0268, "ALC268", patch_alc268),
	HDA_CODEC_ENTRY(0x10ec0269, "ALC269", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0270, "ALC270", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0272, "ALC272", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0274, "ALC274", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0275, "ALC275", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0276, "ALC276", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0280, "ALC280", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0282, "ALC282", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0283, "ALC283", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0284, "ALC284", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0285, "ALC285", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0286, "ALC286", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0287, "ALC287", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0288, "ALC288", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0289, "ALC289", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0290, "ALC290", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0292, "ALC292", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0293, "ALC293", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0294, "ALC294", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0295, "ALC295", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0298, "ALC298", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0299, "ALC299", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0300, "ALC300", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0623, "ALC623", patch_alc269),
	HDA_CODEC_REV_ENTRY(0x10ec0861, 0x100340, "ALC660", patch_alc861),
	HDA_CODEC_ENTRY(0x10ec0660, "ALC660-VD", patch_alc861vd),
	HDA_CODEC_ENTRY(0x10ec0861, "ALC861", patch_alc861),
	HDA_CODEC_ENTRY(0x10ec0862, "ALC861-VD", patch_alc861vd),
	HDA_CODEC_REV_ENTRY(0x10ec0662, 0x100002, "ALC662 rev2", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0662, 0x100101, "ALC662 rev1", patch_alc662),
	HDA_CODEC_REV_ENTRY(0x10ec0662, 0x100300, "ALC662 rev3", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0663, "ALC663", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0665, "ALC665", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0667, "ALC667", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0668, "ALC668", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0670, "ALC670", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0671, "ALC671", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0680, "ALC680", patch_alc680),
	HDA_CODEC_ENTRY(0x10ec0700, "ALC700", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0701, "ALC701", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0703, "ALC703", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0711, "ALC711", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0867, "ALC891", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0880, "ALC880", patch_alc880),
	HDA_CODEC_ENTRY(0x10ec0882, "ALC882", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0883, "ALC883", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0885, 0x100101, "ALC889A", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0885, 0x100103, "ALC889A", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0885, "ALC885", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0887, "ALC887", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0888, 0x100101, "ALC1200", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0888, "ALC888", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0889, "ALC889", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0892, "ALC892", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0899, "ALC898", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0900, "ALC1150", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0b00, "ALCS1200A", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec1168, "ALC1220", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec1220, "ALC1220", patch_alc882),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_realtek);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek HD-audio codec");

static struct hda_codec_driver realtek_driver = {
	.id = snd_hda_id_realtek,
};

module_hda_codec_driver(realtek_driver);
