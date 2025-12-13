// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek HD-audio codec support code
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

static int __alc_read_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int coef_idx)
{
	unsigned int val;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_COEF_INDEX, coef_idx);
	val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_PROC_COEF, 0);
	return val;
}

int alc_read_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			unsigned int coef_idx)
{
	guard(coef_mutex)(codec);
	return __alc_read_coefex_idx(codec, nid, coef_idx);
}
EXPORT_SYMBOL_NS_GPL(alc_read_coefex_idx, "SND_HDA_CODEC_REALTEK");

static void __alc_write_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
				   unsigned int coef_idx, unsigned int coef_val)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_COEF_INDEX, coef_idx);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PROC_COEF, coef_val);
}

void alc_write_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			  unsigned int coef_idx, unsigned int coef_val)
{
	guard(coef_mutex)(codec);
	__alc_write_coefex_idx(codec, nid, coef_idx, coef_val);
}
EXPORT_SYMBOL_NS_GPL(alc_write_coefex_idx, "SND_HDA_CODEC_REALTEK");

static void __alc_update_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
				    unsigned int coef_idx, unsigned int mask,
				    unsigned int bits_set)
{
	unsigned int val = __alc_read_coefex_idx(codec, nid, coef_idx);

	if (val != -1)
		__alc_write_coefex_idx(codec, nid, coef_idx,
				       (val & ~mask) | bits_set);
}

void alc_update_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			   unsigned int coef_idx, unsigned int mask,
			   unsigned int bits_set)
{
	guard(coef_mutex)(codec);
	__alc_update_coefex_idx(codec, nid, coef_idx, mask, bits_set);
}
EXPORT_SYMBOL_NS_GPL(alc_update_coefex_idx, "SND_HDA_CODEC_REALTEK");

/* a special bypass for COEF 0; read the cached value at the second time */
unsigned int alc_get_coef0(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->coef0)
		spec->coef0 = alc_read_coef_idx(codec, 0);
	return spec->coef0;
}
EXPORT_SYMBOL_NS_GPL(alc_get_coef0, "SND_HDA_CODEC_REALTEK");

void alc_process_coef_fw(struct hda_codec *codec, const struct coef_fw *fw)
{
	guard(coef_mutex)(codec);
	for (; fw->nid; fw++) {
		if (fw->mask == (unsigned short)-1)
			__alc_write_coefex_idx(codec, fw->nid, fw->idx, fw->val);
		else
			__alc_update_coefex_idx(codec, fw->nid, fw->idx,
						fw->mask, fw->val);
	}
}
EXPORT_SYMBOL_NS_GPL(alc_process_coef_fw, "SND_HDA_CODEC_REALTEK");

/*
 * GPIO setup tables, used in initialization
 */

/* Enable GPIO mask and set output */
void alc_setup_gpio(struct hda_codec *codec, unsigned int mask)
{
	struct alc_spec *spec = codec->spec;

	spec->gpio_mask |= mask;
	spec->gpio_dir |= mask;
	spec->gpio_data |= mask;
}
EXPORT_SYMBOL_NS_GPL(alc_setup_gpio, "SND_HDA_CODEC_REALTEK");

void alc_write_gpio_data(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
			    spec->gpio_data);
}
EXPORT_SYMBOL_NS_GPL(alc_write_gpio_data, "SND_HDA_CODEC_REALTEK");

void alc_update_gpio_data(struct hda_codec *codec, unsigned int mask,
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
EXPORT_SYMBOL_NS_GPL(alc_update_gpio_data, "SND_HDA_CODEC_REALTEK");

void alc_write_gpio(struct hda_codec *codec)
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
EXPORT_SYMBOL_NS_GPL(alc_write_gpio, "SND_HDA_CODEC_REALTEK");

void alc_fixup_gpio(struct hda_codec *codec, int action, unsigned int mask)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		alc_setup_gpio(codec, mask);
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_gpio, "SND_HDA_CODEC_REALTEK");

void alc_fixup_gpio1(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x01);
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_gpio1, "SND_HDA_CODEC_REALTEK");

void alc_fixup_gpio2(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x02);
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_gpio2, "SND_HDA_CODEC_REALTEK");

void alc_fixup_gpio3(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x03);
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_gpio3, "SND_HDA_CODEC_REALTEK");

void alc_fixup_gpio4(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action)
{
	alc_fixup_gpio(codec, action, 0x04);
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_gpio4, "SND_HDA_CODEC_REALTEK");

void alc_fixup_micmute_led(struct hda_codec *codec,
			   const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_gen_add_micmute_led_cdev(codec, NULL);
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_micmute_led, "SND_HDA_CODEC_REALTEK");

/*
 * Fix hardware PLL issue
 * On some codecs, the analog PLL gating control must be off while
 * the default value is 1.
 */
void alc_fix_pll(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->pll_nid)
		alc_update_coefex_idx(codec, spec->pll_nid, spec->pll_coef_idx,
				      1 << spec->pll_coef_bit, 0);
}
EXPORT_SYMBOL_NS_GPL(alc_fix_pll, "SND_HDA_CODEC_REALTEK");

void alc_fix_pll_init(struct hda_codec *codec, hda_nid_t nid,
		      unsigned int coef_idx, unsigned int coef_bit)
{
	struct alc_spec *spec = codec->spec;
	spec->pll_nid = nid;
	spec->pll_coef_idx = coef_idx;
	spec->pll_coef_bit = coef_bit;
	alc_fix_pll(codec);
}
EXPORT_SYMBOL_NS_GPL(alc_fix_pll_init, "SND_HDA_CODEC_REALTEK");

/* update the master volume per volume-knob's unsol event */
void alc_update_knob_master(struct hda_codec *codec,
			    struct hda_jack_callback *jack)
{
	unsigned int val;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_value *uctl __free(kfree) = NULL;

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
}
EXPORT_SYMBOL_NS_GPL(alc_update_knob_master, "SND_HDA_CODEC_REALTEK");

/* Change EAPD to verb control */
void alc_fill_eapd_coef(struct hda_codec *codec)
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
		fallthrough;
	case 0x10ec0215:
	case 0x10ec0236:
	case 0x10ec0245:
	case 0x10ec0256:
	case 0x10ec0257:
	case 0x10ec0285:
	case 0x10ec0289:
		alc_update_coef_idx(codec, 0x36, 1<<13, 0);
		fallthrough;
	case 0x10ec0230:
	case 0x10ec0233:
	case 0x10ec0235:
	case 0x10ec0255:
	case 0x19e58326:
	case 0x10ec0282:
	case 0x10ec0283:
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0298:
	case 0x10ec0300:
		alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		break;
	case 0x10ec0275:
		alc_update_coef_idx(codec, 0xe, 0, 1<<0);
		break;
	case 0x10ec0287:
		alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		alc_write_coef_idx(codec, 0x8, 0x4ab7);
		break;
	case 0x10ec0293:
		alc_update_coef_idx(codec, 0xa, 1<<13, 0);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
		alc_write_coef_idx(codec, 0x6e, 0x0c25);
		fallthrough;
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
	case 0x10ec0897:
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
EXPORT_SYMBOL_NS_GPL(alc_fill_eapd_coef, "SND_HDA_CODEC_REALTEK");

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
void alc_auto_setup_eapd(struct hda_codec *codec, bool on)
{
	/* We currently only handle front, HP */
	static const hda_nid_t pins[] = {
		0x0f, 0x10, 0x14, 0x15, 0x17, 0
	};
	const hda_nid_t *p;
	for (p = pins; *p; p++)
		set_eapd(codec, *p, on);
}
EXPORT_SYMBOL_NS_GPL(alc_auto_setup_eapd, "SND_HDA_CODEC_REALTEK");

/* Returns the nid of the external mic input pin, or 0 if it cannot be found. */
int alc_find_ext_mic_pin(struct hda_codec *codec)
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
EXPORT_SYMBOL_NS_GPL(alc_find_ext_mic_pin, "SND_HDA_CODEC_REALTEK");

void alc_headset_mic_no_shutup(struct hda_codec *codec)
{
	const struct hda_pincfg *pin;
	int mic_pin = alc_find_ext_mic_pin(codec);
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
EXPORT_SYMBOL_NS_GPL(alc_headset_mic_no_shutup, "SND_HDA_CODEC_REALTEK");

void alc_shutup_pins(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->no_shutup_pins)
		return;

	switch (codec->core.vendor_id) {
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x10ec0257:
	case 0x19e58326:
	case 0x10ec0283:
	case 0x10ec0285:
	case 0x10ec0286:
	case 0x10ec0287:
	case 0x10ec0288:
	case 0x10ec0295:
	case 0x10ec0298:
		alc_headset_mic_no_shutup(codec);
		break;
	default:
		snd_hda_shutup_pins(codec);
		break;
	}
}
EXPORT_SYMBOL_NS_GPL(alc_shutup_pins, "SND_HDA_CODEC_REALTEK");

/* generic shutup callback;
 * just turning off EAPD and a little pause for avoiding pop-noise
 */
void alc_eapd_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	alc_auto_setup_eapd(codec, false);
	if (!spec->no_depop_delay)
		msleep(200);
	alc_shutup_pins(codec);
}
EXPORT_SYMBOL_NS_GPL(alc_eapd_shutup, "SND_HDA_CODEC_REALTEK");

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

/* generic EAPD initialization */
void alc_auto_init_amp(struct hda_codec *codec, int type)
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
EXPORT_SYMBOL_NS_GPL(alc_auto_init_amp, "SND_HDA_CODEC_REALTEK");

/* get a primary headphone pin if available */
hda_nid_t alc_get_hp_pin(struct alc_spec *spec)
{
	if (spec->gen.autocfg.hp_pins[0])
		return spec->gen.autocfg.hp_pins[0];
	if (spec->gen.autocfg.line_out_type == AC_JACK_HP_OUT)
		return spec->gen.autocfg.line_out_pins[0];
	return 0;
}
EXPORT_SYMBOL_NS_GPL(alc_get_hp_pin, "SND_HDA_CODEC_REALTEK");

/*
 * Realtek SSID verification
 */

/* Could be any non-zero and even value. When used as fixup, tells
 * the driver to ignore any present sku defines.
 */
#define ALC_FIXUP_SKU_IGNORE (2)

void alc_fixup_sku_ignore(struct hda_codec *codec,
			  const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->cdefine.fixup = 1;
		spec->cdefine.sku_cfg = ALC_FIXUP_SKU_IGNORE;
	}
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_sku_ignore, "SND_HDA_CODEC_REALTEK");

void alc_fixup_no_depop_delay(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		spec->no_depop_delay = 1;
		codec->depop_delay = 0;
	}
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_no_depop_delay, "SND_HDA_CODEC_REALTEK");

int alc_auto_parse_customize_define(struct hda_codec *codec)
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
EXPORT_SYMBOL_NS_GPL(alc_auto_parse_customize_define, "SND_HDA_CODEC_REALTEK");

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
int alc_subsystem_id(struct hda_codec *codec, const hda_nid_t *ports)
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
			alc_setup_gpio(codec, 0x04);
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
EXPORT_SYMBOL_NS_GPL(alc_subsystem_id, "SND_HDA_CODEC_REALTEK");

/* Check the validity of ALC subsystem-id
 * ports contains an array of 4 pin NIDs for port-A, E, D and I */
void alc_ssid_check(struct hda_codec *codec, const hda_nid_t *ports)
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
EXPORT_SYMBOL_NS_GPL(alc_ssid_check, "SND_HDA_CODEC_REALTEK");

/* inverted digital-mic */
void alc_fixup_inv_dmic(struct hda_codec *codec,
			const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	spec->gen.inv_dmic_split = 1;
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_inv_dmic, "SND_HDA_CODEC_REALTEK");

int alc_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(alc_build_controls, "SND_HDA_CODEC_REALTEK");

int alc_init(struct hda_codec *codec)
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
EXPORT_SYMBOL_NS_GPL(alc_init, "SND_HDA_CODEC_REALTEK");

void alc_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!snd_hda_get_bool_hint(codec, "shutup"))
		return; /* disabled explicitly by hints */

	if (spec && spec->shutup)
		spec->shutup(codec);
	else
		alc_shutup_pins(codec);
}
EXPORT_SYMBOL_NS_GPL(alc_shutup, "SND_HDA_CODEC_REALTEK");

void alc_power_eapd(struct hda_codec *codec)
{
	alc_auto_setup_eapd(codec, false);
}
EXPORT_SYMBOL_NS_GPL(alc_power_eapd, "SND_HDA_CODEC_REALTEK");

int alc_suspend(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc_shutup(codec);
	if (spec && spec->power_hook)
		spec->power_hook(codec);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(alc_suspend, "SND_HDA_CODEC_REALTEK");

int alc_resume(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->no_depop_delay)
		msleep(150); /* to avoid pop noise */
	snd_hda_codec_init(codec);
	snd_hda_regmap_sync(codec);
	hda_call_check_power_status(codec, 0x01);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(alc_resume, "SND_HDA_CODEC_REALTEK");

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
	{ 0x10ec0257, 0x12f0, 0, "ALC3328" },
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
int alc_set_beep_amp(struct alc_spec *spec, hda_nid_t nid, int idx, int dir)
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
EXPORT_SYMBOL_NS_GPL(alc_set_beep_amp, "SND_HDA_CODEC_REALTEK");

static const struct snd_pci_quirk beep_allow_list[] = {
	SND_PCI_QUIRK(0x1043, 0x103c, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x115d, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x829f, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x8376, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x83ce, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x831a, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x834a, "EeePC", 1),
	SND_PCI_QUIRK(0x1458, 0xa002, "GA-MA790X", 1),
	SND_PCI_QUIRK(0x8086, 0xd613, "Intel", 1),
	/* denylist -- no beep available */
	SND_PCI_QUIRK(0x17aa, 0x309e, "Lenovo ThinkCentre M73", 0),
	SND_PCI_QUIRK(0x17aa, 0x30a3, "Lenovo ThinkCentre M93", 0),
	{}
};

int alc_has_cdefine_beep(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct snd_pci_quirk *q;
	q = snd_pci_quirk_lookup(codec->bus->pci, beep_allow_list);
	if (q)
		return q->value;
	return spec->cdefine.enable_pcbeep;
}
EXPORT_SYMBOL_NS_GPL(alc_has_cdefine_beep, "SND_HDA_CODEC_REALTEK");

#endif /* CONFIG_SND_HDA_INPUT_BEEP */

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
int alc_parse_auto_config(struct hda_codec *codec,
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
EXPORT_SYMBOL_NS_GPL(alc_parse_auto_config, "SND_HDA_CODEC_REALTEK");

/* common preparation job for alc_spec */
int alc_alloc_spec(struct hda_codec *codec, hda_nid_t mixer_nid)
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
	codec->forced_resume = 1;
	mutex_init(&spec->coef_mutex);

	err = alc_codec_rename_from_preset(codec);
	if (err < 0) {
		kfree(spec);
		return err;
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(alc_alloc_spec, "SND_HDA_CODEC_REALTEK");

/* For dual-codec configuration, we need to disable some features to avoid
 * conflicts of kctls and PCM streams
 */
void alc_fixup_dual_codecs(struct hda_codec *codec,
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
EXPORT_SYMBOL_NS_GPL(alc_fixup_dual_codecs, "SND_HDA_CODEC_REALTEK");

static const struct snd_pcm_chmap_elem asus_pcm_2_1_chmaps[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 4,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_LFE } }, /* LFE only on right */
	{ }
};

/* override the 2.1 chmap */
void alc_fixup_bass_chmap(struct hda_codec *codec,
			  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_BUILD) {
		struct alc_spec *spec = codec->spec;
		spec->gen.pcm_rec[0]->stream[0].chmap = asus_pcm_2_1_chmaps;
	}
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_bass_chmap, "SND_HDA_CODEC_REALTEK");

/* exported as it's used by multiple codecs */
void alc1220_fixup_gb_dual_codecs(struct hda_codec *codec,
				  const struct hda_fixup *fix,
				  int action)
{
	alc_fixup_dual_codecs(codec, fix, action);
	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* override card longname to provide a unique UCM profile */
		strscpy(codec->card->longname, "HDAudio-Gigabyte-ALC1220DualCodecs");
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
EXPORT_SYMBOL_NS_GPL(alc1220_fixup_gb_dual_codecs, "SND_HDA_CODEC_REALTEK");

void alc233_alc662_fixup_lenovo_dual_codecs(struct hda_codec *codec,
					    const struct hda_fixup *fix,
					    int action)
{
	alc_fixup_dual_codecs(codec, fix, action);
	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* override card longname to provide a unique UCM profile */
		strscpy(codec->card->longname, "HDAudio-Lenovo-DualCodecs");
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
EXPORT_SYMBOL_NS_GPL(alc233_alc662_fixup_lenovo_dual_codecs, "SND_HDA_CODEC_REALTEK");

static void alc_shutup_dell_xps13(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int hp_pin = alc_get_hp_pin(spec);

	/* Prevent pop noises when headphones are plugged in */
	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	msleep(20);
}

void alc_fixup_dell_xps13(struct hda_codec *codec,
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
EXPORT_SYMBOL_NS_GPL(alc_fixup_dell_xps13, "SND_HDA_CODEC_REALTEK");

/*
 * headset handling
 */

static void alc_hp_mute_disable(struct hda_codec *codec, unsigned int delay)
{
	if (delay <= 0)
		delay = 75;
	snd_hda_codec_write(codec, 0x21, 0,
		    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	msleep(delay);
	snd_hda_codec_write(codec, 0x21, 0,
		    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);
	msleep(delay);
}

static void alc_hp_enable_unmute(struct hda_codec *codec, unsigned int delay)
{
	if (delay <= 0)
		delay = 75;
	snd_hda_codec_write(codec, 0x21, 0,
		    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	msleep(delay);
	snd_hda_codec_write(codec, 0x21, 0,
		    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
	msleep(delay);
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
	struct alc_spec *spec = codec->spec;
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

	if (spec->no_internal_mic_pin) {
		alc_update_coef_idx(codec, 0x45, 0xf<<12 | 1<<10, 5<<12);
		return;
	}

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
		alc_hp_mute_disable(codec, 75);
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
		alc_hp_mute_disable(codec, 75);
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
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
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
		fallthrough;
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
		alc_hp_enable_unmute(codec, 75);
		break;
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
		alc_write_coef_idx(codec, 0x1b, 0x0e4b);
		alc_write_coef_idx(codec, 0x45, 0xc089);
		msleep(50);
		alc_process_coef_fw(codec, coef0256);
		alc_hp_enable_unmute(codec, 75);
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
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
		alc_process_coef_fw(codec, coef0256);
		alc_hp_enable_unmute(codec, 75);
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
		alc_hp_enable_unmute(codec, 75);
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
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
		alc_process_coef_fw(codec, coef0256);
		alc_hp_enable_unmute(codec, 75);
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
		alc_hp_enable_unmute(codec, 75);
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

	if (spec->no_internal_mic_pin) {
		alc_update_coef_idx(codec, 0x45, 0xf<<12 | 1<<10, 5<<12);
		return;
	}

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
		alc_write_coef_idx(codec, 0x1b, 0x0e4b);
		alc_write_coef_idx(codec, 0x06, 0x6104);
		alc_write_coefex_idx(codec, 0x57, 0x3, 0x09a3);

		alc_process_coef_fw(codec, coef0255);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		if (!is_ctia) {
			alc_write_coef_idx(codec, 0x45, 0xe089);
			msleep(100);
			val = alc_read_coef_idx(codec, 0x46);
			if ((val & 0x0070) == 0x0070)
				is_ctia = false;
			else
				is_ctia = true;
		}
		alc_write_coefex_idx(codec, 0x57, 0x3, 0x0da3);
		alc_update_coefex_idx(codec, 0x57, 0x5, 1<<14, 0);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		alc_process_coef_fw(codec, coef0274);
		msleep(850);
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
		if (!is_ctia) {
			alc_update_coef_idx(codec, 0x45, 0x3f<<10, 0x38<<10);
			alc_update_coef_idx(codec, 0x49, 3<<8, 1<<8);
			msleep(100);
			val = alc_read_coef_idx(codec, 0x46);
			if ((val & 0x00f0) == 0x00f0)
				is_ctia = false;
			else
				is_ctia = true;
		}
		alc_update_coef_idx(codec, 0x4a, 7<<6, 7<<6);
		alc_update_coef_idx(codec, 0x4a, 3<<4, 3<<4);
		alc_update_coef_idx(codec, 0x67, 0xf000, 0x3000);
		break;
	case 0x10ec0867:
		is_ctia = true;
		break;
	}

	codec_dbg(codec, "Headset jack detected iPhone-style headset: %s\n",
		  str_yes_no(is_ctia));
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

void alc_update_headset_jack_cb(struct hda_codec *codec,
				struct hda_jack_callback *jack)
{
	snd_hda_gen_hp_automute(codec, jack);
	alc_update_headset_mode(codec);
}
EXPORT_SYMBOL_NS_GPL(alc_update_headset_jack_cb, "SND_HDA_CODEC_REALTEK");

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

void alc_fixup_headset_mode(struct hda_codec *codec,
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
EXPORT_SYMBOL_NS_GPL(alc_fixup_headset_mode, "SND_HDA_CODEC_REALTEK");

void alc_fixup_headset_mode_no_hp_mic(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
	}
	else
		alc_fixup_headset_mode(codec, fix, action);
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_headset_mode_no_hp_mic, "SND_HDA_CODEC_REALTEK");

void alc_fixup_headset_mic(struct hda_codec *codec,
			   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_headset_mic, "SND_HDA_CODEC_REALTEK");

/* update LED status via GPIO */
void alc_update_gpio_led(struct hda_codec *codec, unsigned int mask,
			 int polarity, bool enabled)
{
	if (polarity)
		enabled = !enabled;
	alc_update_gpio_data(codec, mask, !enabled); /* muted -> LED on */
}
EXPORT_SYMBOL_NS_GPL(alc_update_gpio_led, "SND_HDA_CODEC_REALTEK");

/* turn on/off mic-mute LED via GPIO per capture hook */
static int micmute_led_set(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	struct hda_codec *codec = dev_to_hda_codec(led_cdev->dev->parent);
	struct alc_spec *spec = codec->spec;

	alc_update_gpio_led(codec, spec->gpio_mic_led_mask,
			    spec->micmute_led_polarity, !brightness);
	return 0;
}

/* turn on/off mute LED via GPIO per vmaster hook */
static int gpio_mute_led_set(struct led_classdev *led_cdev,
			     enum led_brightness brightness)
{
	struct hda_codec *codec = dev_to_hda_codec(led_cdev->dev->parent);
	struct alc_spec *spec = codec->spec;

	alc_update_gpio_led(codec, spec->gpio_mute_led_mask,
			    spec->mute_led_polarity, !brightness);
	return 0;
}

/* setup mute and mic-mute GPIO bits, add hooks appropriately */
void alc_fixup_hp_gpio_led(struct hda_codec *codec,
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
		snd_hda_gen_add_mute_led_cdev(codec, gpio_mute_led_set);
	}
	if (micmute_mask) {
		spec->gpio_mic_led_mask = micmute_mask;
		snd_hda_gen_add_micmute_led_cdev(codec, micmute_led_set);
	}
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_hp_gpio_led, "SND_HDA_CODEC_REALTEK");

/* suppress the jack-detection */
void alc_fixup_no_jack_detect(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		codec->no_jack_detect = 1;
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_no_jack_detect, "SND_HDA_CODEC_REALTEK");

void alc_fixup_disable_aamix(struct hda_codec *codec,
			     const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		/* Disable AA-loopback as it causes white noise */
		spec->gen.mixer_nid = 0;
	}
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_disable_aamix, "SND_HDA_CODEC_REALTEK");

void alc_fixup_auto_mute_via_amp(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->gen.auto_mute_via_amp = 1;
	}
}
EXPORT_SYMBOL_NS_GPL(alc_fixup_auto_mute_via_amp, "SND_HDA_CODEC_REALTEK");

MODULE_IMPORT_NS("SND_HDA_SCODEC_COMPONENT");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek HD-audio codec helper");
