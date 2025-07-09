// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC662 and compatible codecs
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

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

static void alc897_hp_automute_hook(struct hda_codec *codec,
					 struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	int vref;

	snd_hda_gen_hp_automute(codec, jack);
	vref = spec->gen.hp_jack_present ? (PIN_HP | AC_PINCTL_VREF_100) : PIN_HP;
	snd_hda_set_pin_ctl(codec, 0x1b, vref);
}

static void alc897_fixup_lenovo_headset_mic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.hp_automute_hook = alc897_hp_automute_hook;
		spec->no_shutup_pins = 1;
	}
	if (action == HDA_FIXUP_ACT_PROBE) {
		snd_hda_set_pin_ctl_cache(codec, 0x1a, PIN_IN | AC_PINCTL_VREF_100);
	}
}

static void alc897_fixup_lenovo_headset_mode(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		spec->gen.hp_automute_hook = alc897_hp_automute_hook;
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

enum {
	ALC662_FIXUP_ASPIRE,
	ALC662_FIXUP_LED_GPIO1,
	ALC662_FIXUP_IDEAPAD,
	ALC272_FIXUP_MARIO,
	ALC662_FIXUP_CZC_ET26,
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
	ALC668_FIXUP_ASUS_NO_HEADSET_MIC,
	ALC668_FIXUP_HEADSET_MIC,
	ALC668_FIXUP_MIC_DET_COEF,
	ALC897_FIXUP_LENOVO_HEADSET_MIC,
	ALC897_FIXUP_HEADSET_MIC_PIN,
	ALC897_FIXUP_HP_HSMIC_VERB,
	ALC897_FIXUP_LENOVO_HEADSET_MODE,
	ALC897_FIXUP_HEADSET_MIC_PIN2,
	ALC897_FIXUP_UNIS_H3C_X500S,
	ALC897_FIXUP_HEADSET_MIC_PIN3,
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
	[ALC662_FIXUP_CZC_ET26] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x12, 0x403cc000},
			{0x14, 0x90170110}, /* speaker */
			{0x15, 0x411111f0},
			{0x16, 0x411111f0},
			{0x18, 0x01a19030}, /* mic */
			{0x19, 0x90a7013f}, /* int-mic */
			{0x1a, 0x01014020},
			{0x1b, 0x0121401f},
			{0x1c, 0x411111f0},
			{0x1d, 0x411111f0},
			{0x1e, 0x40478e35},
			{}
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
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
	[ALC668_FIXUP_ASUS_NO_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x04a1112c },
			{ }
		},
		.chained = true,
		.chain_id = ALC668_FIXUP_HEADSET_MIC
	},
	[ALC668_FIXUP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mic,
		.chained = true,
		.chain_id = ALC668_FIXUP_MIC_DET_COEF
	},
	[ALC668_FIXUP_MIC_DET_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x15 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0d60 },
			{}
		},
	},
	[ALC897_FIXUP_LENOVO_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc897_fixup_lenovo_headset_mic,
	},
	[ALC897_FIXUP_HEADSET_MIC_PIN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x03a11050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC897_FIXUP_LENOVO_HEADSET_MIC
	},
	[ALC897_FIXUP_HP_HSMIC_VERB] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
	},
	[ALC897_FIXUP_LENOVO_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc897_fixup_lenovo_headset_mode,
	},
	[ALC897_FIXUP_HEADSET_MIC_PIN2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x01a11140 }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC897_FIXUP_LENOVO_HEADSET_MODE
	},
	[ALC897_FIXUP_UNIS_H3C_X500S] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x14, AC_VERB_SET_EAPD_BTLENABLE, 0 },
			{}
		},
	},
	[ALC897_FIXUP_HEADSET_MIC_PIN3] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 }, /* use as headset mic */
			{ }
		},
	},
};

static const struct hda_quirk alc662_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x9087, "ECS", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1019, 0x9859, "JP-IK LEAP W502", ALC897_FIXUP_HEADSET_MIC_PIN3),
	SND_PCI_QUIRK(0x1025, 0x022f, "Acer Aspire One", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0241, "Packard Bell DOTS", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0308, "Acer Aspire 8942G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x031c, "Gateway NV79", ALC662_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x1025, 0x0349, "eMachines eM250", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x034a, "Gateway LT27", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x038b, "Acer Aspire 8943G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0566, "Acer Aspire Ethos 8951G", ALC669_FIXUP_ACER_ASPIRE_ETHOS),
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
	SND_PCI_QUIRK(0x103c, 0x870c, "HP", ALC897_FIXUP_HP_HSMIC_VERB),
	SND_PCI_QUIRK(0x103c, 0x8719, "HP", ALC897_FIXUP_HP_HSMIC_VERB),
	SND_PCI_QUIRK(0x103c, 0x872b, "HP", ALC897_FIXUP_HP_HSMIC_VERB),
	SND_PCI_QUIRK(0x103c, 0x873e, "HP", ALC671_FIXUP_HP_HEADSET_MIC2),
	SND_PCI_QUIRK(0x103c, 0x8768, "HP Slim Desktop S01", ALC671_FIXUP_HP_HEADSET_MIC2),
	SND_PCI_QUIRK(0x103c, 0x877e, "HP 288 Pro G6", ALC671_FIXUP_HP_HEADSET_MIC2),
	SND_PCI_QUIRK(0x103c, 0x885f, "HP 288 Pro G8", ALC671_FIXUP_HP_HEADSET_MIC2),
	SND_PCI_QUIRK(0x1043, 0x1080, "Asus UX501VW", ALC668_FIXUP_HEADSET_MODE),
	SND_PCI_QUIRK(0x1043, 0x11cd, "Asus N550", ALC662_FIXUP_ASUS_Nx50),
	SND_PCI_QUIRK(0x1043, 0x129d, "Asus N750", ALC662_FIXUP_ASUS_Nx50),
	SND_PCI_QUIRK(0x1043, 0x12ff, "ASUS G751", ALC668_FIXUP_ASUS_G751),
	SND_PCI_QUIRK(0x1043, 0x13df, "Asus N550JX", ALC662_FIXUP_BASS_1A),
	SND_PCI_QUIRK(0x1043, 0x1477, "ASUS N56VZ", ALC662_FIXUP_BASS_MODE4_CHMAP),
	SND_PCI_QUIRK(0x1043, 0x15a7, "ASUS UX51VZH", ALC662_FIXUP_BASS_16),
	SND_PCI_QUIRK(0x1043, 0x177d, "ASUS N551", ALC668_FIXUP_ASUS_Nx51),
	SND_PCI_QUIRK(0x1043, 0x17bd, "ASUS N751", ALC668_FIXUP_ASUS_Nx51),
	SND_PCI_QUIRK(0x1043, 0x185d, "ASUS G551JW", ALC668_FIXUP_ASUS_NO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71SL", ALC662_FIXUP_ASUS_MODE8),
	SND_PCI_QUIRK(0x1043, 0x1b73, "ASUS N55SF", ALC662_FIXUP_BASS_16),
	SND_PCI_QUIRK(0x1043, 0x1bf3, "ASUS N76VZ", ALC662_FIXUP_BASS_MODE4_CHMAP),
	SND_PCI_QUIRK(0x1043, 0x8469, "ASUS mobo", ALC662_FIXUP_NO_JACK_DETECT),
	SND_PCI_QUIRK(0x105b, 0x0cd6, "Foxconn", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x144d, 0xc051, "Samsung R720", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x14cd, 0x5003, "USI", ALC662_FIXUP_USI_HEADSET_MODE),
	SND_PCI_QUIRK(0x17aa, 0x1036, "Lenovo P520", ALC662_FIXUP_LENOVO_MULTI_CODECS),
	SND_PCI_QUIRK(0x17aa, 0x1057, "Lenovo P360", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x1064, "Lenovo P3 Tower", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x32ca, "Lenovo ThinkCentre M80", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x32cb, "Lenovo ThinkCentre M70", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x32cf, "Lenovo ThinkCentre M950", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x32f7, "Lenovo ThinkCentre M90", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x3321, "Lenovo ThinkCentre M70 Gen4", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x331b, "Lenovo ThinkCentre M90 Gen4", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x3364, "Lenovo ThinkCentre M90 Gen5", ALC897_FIXUP_HEADSET_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x3742, "Lenovo TianYi510Pro-14IOB", ALC897_FIXUP_HEADSET_MIC_PIN2),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo Ideapad Y550P", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Ideapad Y550", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x1849, 0x5892, "ASRock B150M", ALC892_FIXUP_ASROCK_MOBO),
	SND_PCI_QUIRK(0x19da, 0xa130, "Zotac Z68", ALC662_FIXUP_ZOTAC_Z68),
	SND_PCI_QUIRK(0x1b0a, 0x01b8, "ACER Veriton", ALC662_FIXUP_ACER_VERITON),
	SND_PCI_QUIRK(0x1b35, 0x1234, "CZC ET26", ALC662_FIXUP_CZC_ET26),
	SND_PCI_QUIRK(0x1b35, 0x2206, "CZC P10T", ALC662_FIXUP_CZC_P10T),
	SND_PCI_QUIRK(0x1c6c, 0x1239, "Compaq N14JP6-V2", ALC897_FIXUP_HP_HSMIC_VERB),

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
	{.id = ALC897_FIXUP_UNIS_H3C_X500S, .name = "unis-h3c-x500s"},
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
static int alc662_probe(struct hda_codec *codec, const struct hda_device_id *id)
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
	snd_hda_gen_remove(codec);
	return err;
}

static const struct hda_codec_ops alc662_codec_ops = {
	.probe = alc662_probe,
	.remove = snd_hda_gen_remove,
	.build_controls = alc_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = alc_init,
	.unsol_event = snd_hda_jack_unsol_event,
	.resume = alc_resume,
	.suspend = alc_suspend,
	.check_power_status = snd_hda_gen_check_power_status,
	.stream_pm = snd_hda_gen_stream_pm,
};

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_alc662[] = {
	HDA_CODEC_ID(0x10ec0272, "ALC272"),
	HDA_CODEC_ID_REV(0x10ec0662, 0x100101, "ALC662 rev1"),
	HDA_CODEC_ID_REV(0x10ec0662, 0x100300, "ALC662 rev3"),
	HDA_CODEC_ID(0x10ec0663, "ALC663"),
	HDA_CODEC_ID(0x10ec0665, "ALC665"),
	HDA_CODEC_ID(0x10ec0667, "ALC667"),
	HDA_CODEC_ID(0x10ec0668, "ALC668"),
	HDA_CODEC_ID(0x10ec0670, "ALC670"),
	HDA_CODEC_ID(0x10ec0671, "ALC671"),
	HDA_CODEC_ID(0x10ec0867, "ALC891"),
	HDA_CODEC_ID(0x10ec0892, "ALC892"),
	HDA_CODEC_ID(0x10ec0897, "ALC897"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc662);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC662 and compatible HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");

static struct hda_codec_driver alc662_driver = {
	.id = snd_hda_id_alc662,
	.ops = &alc662_codec_ops,
};

module_hda_codec_driver(alc662_driver);
