// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek ALC269 and compatible codecs
//

#include <linux/init.h>
#include <linux/module.h>
#include "realtek.h"

/* keep halting ALC5505 DSP, for power saving */
#define HALT_REALTEK_ALC5505

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
	ALC269_TYPE_ALC245,
	ALC269_TYPE_ALC287,
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
	case ALC269_TYPE_ALC245:
	case ALC269_TYPE_ALC287:
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

	snd_hda_jack_set_button_state(codec, jack->nid, report);
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
	case 0x10ec0287:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_write_coef_idx(codec, 0x48, 0x0);
		alc_update_coef_idx(codec, 0x49, 0x0045, 0x0);
		alc_update_coef_idx(codec, 0x44, 0x0045 << 8, 0x0);
		break;
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x10ec0257:
	case 0x19e58326:
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
	case 0x10ec0287:
	case 0x10ec0295:
	case 0x10ec0289:
	case 0x10ec0299:
		alc_write_coef_idx(codec, 0x48, 0xd011);
		alc_update_coef_idx(codec, 0x49, 0x007f, 0x0045);
		alc_update_coef_idx(codec, 0x44, 0x007f << 8, 0x0045 << 8);
		break;
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x10ec0257:
	case 0x19e58326:
		alc_write_coef_idx(codec, 0x48, 0xd011);
		alc_update_coef_idx(codec, 0x49, 0x007f, 0x0045);
		break;
	}
}

static void alc_fixup_headset_jack(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->has_hs_key = 1;
		snd_hda_jack_detect_enable_callback(codec, 0x55,
						    alc_headset_btn_callback);
		break;
	case HDA_FIXUP_ACT_BUILD:
		hp_pin = alc_get_hp_pin(spec);
		if (!hp_pin || snd_hda_jack_bind_keymap(codec, 0x55,
							alc_headset_btn_keymap,
							hp_pin))
			snd_hda_jack_add_kctl(codec, 0x55, "Headset Jack",
					      false, SND_JACK_HEADSET,
					      alc_headset_btn_keymap);

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

	if (spec->ultra_low_power) {
		alc_update_coef_idx(codec, 0x03, 1<<1, 1<<1);
		alc_update_coef_idx(codec, 0x08, 3<<2, 3<<2);
		alc_update_coef_idx(codec, 0x08, 7<<4, 0);
		alc_update_coef_idx(codec, 0x3b, 1<<15, 0);
		alc_update_coef_idx(codec, 0x0e, 7<<6, 7<<6);
		msleep(30);
	}

	if (!hp_pin)
		hp_pin = 0x21;

	msleep(30);

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	if (hp_pin_sense) {
		msleep(2);
		alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x1); /* Low power */

		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

		msleep(75);

		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);

		msleep(75);
		alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x4); /* Hight power */
	}
	alc_update_coef_idx(codec, 0x46, 3 << 12, 0);
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

	alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x1); /* Low power */

	/* 3k pull low control for Headset jack. */
	/* NOTE: call this before clearing the pin, otherwise codec stalls */
	/* If disable 3k pulldown control for alc257, the Mic detection will not work correctly
	 * when booting with headset plugged. So skip setting it for the codec alc257
	 */
	if (spec->en_3kpull_low)
		alc_update_coef_idx(codec, 0x46, 0, 3 << 12);

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	if (hp_pin_sense) {
		msleep(2);

		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

		msleep(75);

		if (!spec->no_shutup_pins)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

		msleep(75);
	}

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

static void alc285_hp_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	int i, val;
	int coef38, coef0d, coef36;

	alc_write_coefex_idx(codec, 0x58, 0x00, 0x1888); /* write default value */
	alc_update_coef_idx(codec, 0x4a, 1<<15, 1<<15); /* Reset HP JD */
	coef38 = alc_read_coef_idx(codec, 0x38); /* Amp control */
	coef0d = alc_read_coef_idx(codec, 0x0d); /* Digital Misc control */
	coef36 = alc_read_coef_idx(codec, 0x36); /* Passthrough Control */
	alc_update_coef_idx(codec, 0x38, 1<<4, 0x0);
	alc_update_coef_idx(codec, 0x0d, 0x110, 0x0);

	alc_update_coef_idx(codec, 0x67, 0xf000, 0x3000);

	if (hp_pin)
		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	msleep(130);
	alc_update_coef_idx(codec, 0x36, 1<<14, 1<<14);
	alc_update_coef_idx(codec, 0x36, 1<<13, 0x0);

	if (hp_pin)
		snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);
	msleep(10);
	alc_write_coef_idx(codec, 0x67, 0x0); /* Set HP depop to manual mode */
	alc_write_coefex_idx(codec, 0x58, 0x00, 0x7880);
	alc_write_coefex_idx(codec, 0x58, 0x0f, 0xf049);
	alc_update_coefex_idx(codec, 0x58, 0x03, 0x00f0, 0x00c0);

	alc_write_coefex_idx(codec, 0x58, 0x00, 0xf888); /* HP depop procedure start */
	val = alc_read_coefex_idx(codec, 0x58, 0x00);
	for (i = 0; i < 20 && val & 0x8000; i++) {
		msleep(50);
		val = alc_read_coefex_idx(codec, 0x58, 0x00);
	} /* Wait for depop procedure finish  */

	alc_write_coefex_idx(codec, 0x58, 0x00, val); /* write back the result */
	alc_update_coef_idx(codec, 0x38, 1<<4, coef38);
	alc_update_coef_idx(codec, 0x0d, 0x110, coef0d);
	alc_update_coef_idx(codec, 0x36, 3<<13, coef36);

	msleep(50);
	alc_update_coef_idx(codec, 0x4a, 1<<15, 0);
}

static void alc225_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp1_pin_sense, hp2_pin_sense;

	if (spec->ultra_low_power) {
		alc_update_coef_idx(codec, 0x08, 0x0f << 2, 3<<2);
		alc_update_coef_idx(codec, 0x0e, 7<<6, 7<<6);
		alc_update_coef_idx(codec, 0x33, 1<<11, 0);
		msleep(30);
	}

	if (spec->codec_variant != ALC269_TYPE_ALC287 &&
		spec->codec_variant != ALC269_TYPE_ALC245)
		/* required only at boot or S3 and S4 resume time */
		if (!spec->done_hp_init ||
			is_s3_resume(codec) ||
			is_s4_resume(codec)) {
			alc285_hp_init(codec);
			spec->done_hp_init = true;
		}

	if (!hp_pin)
		hp_pin = 0x21;
	msleep(30);

	hp1_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	hp2_pin_sense = snd_hda_jack_detect(codec, 0x16);

	if (hp1_pin_sense || hp2_pin_sense) {
		msleep(2);
		alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x1); /* Low power */

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x16, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		msleep(75);

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x16, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);

		msleep(75);
		alc_update_coef_idx(codec, 0x4a, 3 << 10, 0);
		alc_update_coefex_idx(codec, 0x57, 0x04, 0x0007, 0x4); /* Hight power */
	}
}

static void alc225_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp1_pin_sense, hp2_pin_sense;

	if (!hp_pin)
		hp_pin = 0x21;

	hp1_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	hp2_pin_sense = snd_hda_jack_detect(codec, 0x16);

	if (hp1_pin_sense || hp2_pin_sense) {
		alc_disable_headset_jack_key(codec);
		/* 3k pull low control for Headset jack. */
		alc_update_coef_idx(codec, 0x4a, 0, 3 << 10);
		msleep(2);

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x16, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

		msleep(75);

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x16, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

		msleep(75);
		alc_update_coef_idx(codec, 0x4a, 3 << 10, 0);
		alc_enable_headset_jack_key(codec);
	}
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
}

static void alc222_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp1_pin_sense, hp2_pin_sense;

	if (!hp_pin)
		return;

	msleep(30);

	hp1_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	hp2_pin_sense = snd_hda_jack_detect(codec, 0x14);

	if (hp1_pin_sense || hp2_pin_sense) {
		msleep(2);

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x14, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		msleep(75);

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x14, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);

		msleep(75);
	}
}

static void alc222_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = alc_get_hp_pin(spec);
	bool hp1_pin_sense, hp2_pin_sense;

	if (!hp_pin)
		hp_pin = 0x21;

	hp1_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	hp2_pin_sense = snd_hda_jack_detect(codec, 0x14);

	if (hp1_pin_sense || hp2_pin_sense) {
		msleep(2);

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x14, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

		msleep(75);

		if (hp1_pin_sense)
			snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);
		if (hp2_pin_sense)
			snd_hda_codec_write(codec, 0x14, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

		msleep(75);
	}
	alc_auto_setup_eapd(codec, false);
	alc_shutup_pins(codec);
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

	if (hp_pin_sense) {
		msleep(2);

		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

		msleep(75);

		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
		msleep(75);
	}
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

	if (hp_pin_sense) {
		msleep(2);

		snd_hda_codec_write(codec, hp_pin, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

		msleep(75);

		if (!spec->no_shutup_pins)
			snd_hda_codec_write(codec, hp_pin, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

		msleep(75);
	}
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
	if (!spec->done_hp_init || is_s4_resume(codec)) {
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

	snd_hda_codec_init(codec);

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

/* Fix the speaker amp after resume, etc */
static void alc269vb_fixup_aspire_e1_coef(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	if (action == HDA_FIXUP_ACT_INIT)
		alc_update_coef_idx(codec, 0x0d, 0x6000, 0x6000);
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

static void alc_update_vref_led(struct hda_codec *codec, hda_nid_t pin,
				bool polarity, bool on)
{
	unsigned int pinval;

	if (!pin)
		return;
	if (polarity)
		on = !on;
	pinval = snd_hda_codec_get_pin_target(codec, pin);
	pinval &= ~AC_PINCTL_VREFEN;
	pinval |= on ? AC_PINCTL_VREF_80 : AC_PINCTL_VREF_HIZ;
	/* temporarily power up/down for setting VREF */
	CLASS(snd_hda_power_pm, pm)(codec);
	snd_hda_set_pin_ctl_cache(codec, pin, pinval);
}

/* update mute-LED according to the speaker mute state via mic VREF pin */
static int vref_mute_led_set(struct led_classdev *led_cdev,
			     enum led_brightness brightness)
{
	struct hda_codec *codec = dev_to_hda_codec(led_cdev->dev->parent);
	struct alc_spec *spec = codec->spec;

	alc_update_vref_led(codec, spec->mute_led_nid,
			    spec->mute_led_polarity, brightness);
	return 0;
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
		snd_hda_gen_add_mute_led_cdev(codec, vref_mute_led_set);
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
		snd_hda_gen_add_mute_led_cdev(codec, vref_mute_led_set);
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

static void alc236_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x02, 0x01);
}

static void alc269_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x08, 0x10);
}

static void alc285_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x04, 0x01);
}

static void alc286_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x02, 0x20);
}

static void alc287_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0x10, 0);
}

static void alc245_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->micmute_led_polarity = 1;
	alc_fixup_hp_gpio_led(codec, action, 0, 0x04);
}

/* turn on/off mic-mute LED per capture hook via VREF change */
static int vref_micmute_led_set(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct hda_codec *codec = dev_to_hda_codec(led_cdev->dev->parent);
	struct alc_spec *spec = codec->spec;

	alc_update_vref_led(codec, spec->cap_mute_led_nid,
			    spec->micmute_led_polarity, brightness);
	return 0;
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
		snd_hda_gen_add_micmute_led_cdev(codec, vref_micmute_led_set);
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
		snd_hda_gen_add_micmute_led_cdev(codec, vref_micmute_led_set);
		codec->power_filter = led_power_filter;
	}
}

/* HP Spectre x360 14 model needs a unique workaround for enabling the amp;
 * it needs to toggle the GPIO0 once on and off at each time (bko#210633)
 */
static void alc245_fixup_hp_x360_amp(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gpio_mask |= 0x01;
		spec->gpio_dir |= 0x01;
		break;
	case HDA_FIXUP_ACT_INIT:
		/* need to toggle GPIO to enable the amp */
		alc_update_gpio_data(codec, 0x01, true);
		msleep(100);
		alc_update_gpio_data(codec, 0x01, false);
		break;
	}
}

/* toggle GPIO2 at each time stream is started; we use PREPARE state instead */
static void alc274_hp_envy_pcm_hook(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream,
				    int action)
{
	switch (action) {
	case HDA_GEN_PCM_ACT_PREPARE:
		alc_update_gpio_data(codec, 0x04, true);
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		alc_update_gpio_data(codec, 0x04, false);
		break;
	}
}

static void alc274_fixup_hp_envy_gpio(struct hda_codec *codec,
				      const struct hda_fixup *fix,
				      int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		spec->gpio_mask |= 0x04;
		spec->gpio_dir |= 0x04;
		spec->gen.pcm_playback_hook = alc274_hp_envy_pcm_hook;
	}
}

static void alc_update_coef_led(struct hda_codec *codec,
				struct alc_coef_led *led,
				bool polarity, bool on)
{
	if (polarity)
		on = !on;
	/* temporarily power up/down for setting COEF bit */
	alc_update_coef_idx(codec, led->idx, led->mask,
			    on ? led->on : led->off);
}

/* update mute-LED according to the speaker mute state via COEF bit */
static int coef_mute_led_set(struct led_classdev *led_cdev,
			     enum led_brightness brightness)
{
	struct hda_codec *codec = dev_to_hda_codec(led_cdev->dev->parent);
	struct alc_spec *spec = codec->spec;

	alc_update_coef_led(codec, &spec->mute_led_coef,
			    spec->mute_led_polarity, brightness);
	return 0;
}

static void alc285_fixup_hp_mute_led_coefbit(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef.idx = 0x0b;
		spec->mute_led_coef.mask = 1 << 3;
		spec->mute_led_coef.on = 1 << 3;
		spec->mute_led_coef.off = 0;
		snd_hda_gen_add_mute_led_cdev(codec, coef_mute_led_set);
	}
}

static void alc236_fixup_hp_mute_led_coefbit(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef.idx = 0x34;
		spec->mute_led_coef.mask = 1 << 5;
		spec->mute_led_coef.on = 0;
		spec->mute_led_coef.off = 1 << 5;
		snd_hda_gen_add_mute_led_cdev(codec, coef_mute_led_set);
	}
}

static void alc236_fixup_hp_mute_led_coefbit2(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef.idx = 0x07;
		spec->mute_led_coef.mask = 1;
		spec->mute_led_coef.on = 1;
		spec->mute_led_coef.off = 0;
		snd_hda_gen_add_mute_led_cdev(codec, coef_mute_led_set);
	}
}

static void alc245_fixup_hp_mute_led_coefbit(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef.idx = 0x0b;
		spec->mute_led_coef.mask = 3 << 2;
		spec->mute_led_coef.on = 2 << 2;
		spec->mute_led_coef.off = 1 << 2;
		snd_hda_gen_add_mute_led_cdev(codec, coef_mute_led_set);
	}
}

static void alc245_fixup_hp_mute_led_v1_coefbit(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef.idx = 0x0b;
		spec->mute_led_coef.mask = 3 << 2;
		spec->mute_led_coef.on = 1 << 3;
		spec->mute_led_coef.off = 0;
		snd_hda_gen_add_mute_led_cdev(codec, coef_mute_led_set);
	}
}

/* turn on/off mic-mute LED per capture hook by coef bit */
static int coef_micmute_led_set(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct hda_codec *codec = dev_to_hda_codec(led_cdev->dev->parent);
	struct alc_spec *spec = codec->spec;

	alc_update_coef_led(codec, &spec->mic_led_coef,
			    spec->micmute_led_polarity, brightness);
	return 0;
}

static void alc285_fixup_hp_coef_micmute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mic_led_coef.idx = 0x19;
		spec->mic_led_coef.mask = 1 << 13;
		spec->mic_led_coef.on = 1 << 13;
		spec->mic_led_coef.off = 0;
		snd_hda_gen_add_micmute_led_cdev(codec, coef_micmute_led_set);
	}
}

static void alc285_fixup_hp_gpio_micmute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->micmute_led_polarity = 1;
	alc_fixup_hp_gpio_led(codec, action, 0, 0x04);
}

static void alc236_fixup_hp_coef_micmute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mic_led_coef.idx = 0x35;
		spec->mic_led_coef.mask = 3 << 2;
		spec->mic_led_coef.on = 2 << 2;
		spec->mic_led_coef.off = 1 << 2;
		snd_hda_gen_add_micmute_led_cdev(codec, coef_micmute_led_set);
	}
}

static void alc295_fixup_hp_mute_led_coefbit11(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_coef.idx = 0xb;
		spec->mute_led_coef.mask = 3 << 3;
		spec->mute_led_coef.on = 1 << 3;
		spec->mute_led_coef.off = 1 << 4;
		snd_hda_gen_add_mute_led_cdev(codec, coef_mute_led_set);
	}
}

static void alc285_fixup_hp_mute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc285_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc285_fixup_hp_coef_micmute_led(codec, fix, action);
}

static void alc285_fixup_hp_spectre_x360_mute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc285_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc285_fixup_hp_gpio_micmute_led(codec, fix, action);
}

static void alc236_fixup_hp_mute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc236_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc236_fixup_hp_coef_micmute_led(codec, fix, action);
}

static void alc236_fixup_hp_micmute_led_vref(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->cap_mute_led_nid = 0x1a;
		snd_hda_gen_add_micmute_led_cdev(codec, vref_micmute_led_set);
		codec->power_filter = led_power_filter;
	}
}

static void alc236_fixup_hp_mute_led_micmute_vref(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc236_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc236_fixup_hp_micmute_led_vref(codec, fix, action);
}

static inline void alc298_samsung_write_coef_pack(struct hda_codec *codec,
						  const unsigned short coefs[2])
{
	alc_write_coef_idx(codec, 0x23, coefs[0]);
	alc_write_coef_idx(codec, 0x25, coefs[1]);
	alc_write_coef_idx(codec, 0x26, 0xb011);
}

struct alc298_samsung_amp_desc {
	unsigned char nid;
	unsigned short init_seq[2][2];
};

static void alc298_fixup_samsung_amp(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	int i, j;
	static const unsigned short init_seq[][2] = {
		{ 0x19, 0x00 }, { 0x20, 0xc0 }, { 0x22, 0x44 }, { 0x23, 0x08 },
		{ 0x24, 0x85 }, { 0x25, 0x41 }, { 0x35, 0x40 }, { 0x36, 0x01 },
		{ 0x38, 0x81 }, { 0x3a, 0x03 }, { 0x3b, 0x81 }, { 0x40, 0x3e },
		{ 0x41, 0x07 }, { 0x400, 0x1 }
	};
	static const struct alc298_samsung_amp_desc amps[] = {
		{ 0x3a, { { 0x18, 0x1 }, { 0x26, 0x0 } } },
		{ 0x39, { { 0x18, 0x2 }, { 0x26, 0x1 } } }
	};

	if (action != HDA_FIXUP_ACT_INIT)
		return;

	for (i = 0; i < ARRAY_SIZE(amps); i++) {
		alc_write_coef_idx(codec, 0x22, amps[i].nid);

		for (j = 0; j < ARRAY_SIZE(amps[i].init_seq); j++)
			alc298_samsung_write_coef_pack(codec, amps[i].init_seq[j]);

		for (j = 0; j < ARRAY_SIZE(init_seq); j++)
			alc298_samsung_write_coef_pack(codec, init_seq[j]);
	}
}

struct alc298_samsung_v2_amp_desc {
	unsigned short nid;
	int init_seq_size;
	unsigned short init_seq[18][2];
};

static const struct alc298_samsung_v2_amp_desc
alc298_samsung_v2_amp_desc_tbl[] = {
	{ 0x38, 18, {
		{ 0x23e1, 0x0000 }, { 0x2012, 0x006f }, { 0x2014, 0x0000 },
		{ 0x201b, 0x0001 }, { 0x201d, 0x0001 }, { 0x201f, 0x00fe },
		{ 0x2021, 0x0000 }, { 0x2022, 0x0010 }, { 0x203d, 0x0005 },
		{ 0x203f, 0x0003 }, { 0x2050, 0x002c }, { 0x2076, 0x000e },
		{ 0x207c, 0x004a }, { 0x2081, 0x0003 }, { 0x2399, 0x0003 },
		{ 0x23a4, 0x00b5 }, { 0x23a5, 0x0001 }, { 0x23ba, 0x0094 }
	}},
	{ 0x39, 18, {
		{ 0x23e1, 0x0000 }, { 0x2012, 0x006f }, { 0x2014, 0x0000 },
		{ 0x201b, 0x0002 }, { 0x201d, 0x0002 }, { 0x201f, 0x00fd },
		{ 0x2021, 0x0001 }, { 0x2022, 0x0010 }, { 0x203d, 0x0005 },
		{ 0x203f, 0x0003 }, { 0x2050, 0x002c }, { 0x2076, 0x000e },
		{ 0x207c, 0x004a }, { 0x2081, 0x0003 }, { 0x2399, 0x0003 },
		{ 0x23a4, 0x00b5 }, { 0x23a5, 0x0001 }, { 0x23ba, 0x0094 }
	}},
	{ 0x3c, 15, {
		{ 0x23e1, 0x0000 }, { 0x2012, 0x006f }, { 0x2014, 0x0000 },
		{ 0x201b, 0x0001 }, { 0x201d, 0x0001 }, { 0x201f, 0x00fe },
		{ 0x2021, 0x0000 }, { 0x2022, 0x0010 }, { 0x203d, 0x0005 },
		{ 0x203f, 0x0003 }, { 0x2050, 0x002c }, { 0x2076, 0x000e },
		{ 0x207c, 0x004a }, { 0x2081, 0x0003 }, { 0x23ba, 0x008d }
	}},
	{ 0x3d, 15, {
		{ 0x23e1, 0x0000 }, { 0x2012, 0x006f }, { 0x2014, 0x0000 },
		{ 0x201b, 0x0002 }, { 0x201d, 0x0002 }, { 0x201f, 0x00fd },
		{ 0x2021, 0x0001 }, { 0x2022, 0x0010 }, { 0x203d, 0x0005 },
		{ 0x203f, 0x0003 }, { 0x2050, 0x002c }, { 0x2076, 0x000e },
		{ 0x207c, 0x004a }, { 0x2081, 0x0003 }, { 0x23ba, 0x008d }
	}}
};

static void alc298_samsung_v2_enable_amps(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	static const unsigned short enable_seq[][2] = {
		{ 0x203a, 0x0081 }, { 0x23ff, 0x0001 },
	};
	int i, j;

	for (i = 0; i < spec->num_speaker_amps; i++) {
		alc_write_coef_idx(codec, 0x22, alc298_samsung_v2_amp_desc_tbl[i].nid);
		for (j = 0; j < ARRAY_SIZE(enable_seq); j++)
			alc298_samsung_write_coef_pack(codec, enable_seq[j]);
		codec_dbg(codec, "alc298_samsung_v2: Enabled speaker amp 0x%02x\n",
				alc298_samsung_v2_amp_desc_tbl[i].nid);
	}
}

static void alc298_samsung_v2_disable_amps(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	static const unsigned short disable_seq[][2] = {
		{ 0x23ff, 0x0000 }, { 0x203a, 0x0080 },
	};
	int i, j;

	for (i = 0; i < spec->num_speaker_amps; i++) {
		alc_write_coef_idx(codec, 0x22, alc298_samsung_v2_amp_desc_tbl[i].nid);
		for (j = 0; j < ARRAY_SIZE(disable_seq); j++)
			alc298_samsung_write_coef_pack(codec, disable_seq[j]);
		codec_dbg(codec, "alc298_samsung_v2: Disabled speaker amp 0x%02x\n",
				alc298_samsung_v2_amp_desc_tbl[i].nid);
	}
}

static void alc298_samsung_v2_playback_hook(struct hda_pcm_stream *hinfo,
				struct hda_codec *codec,
				struct snd_pcm_substream *substream,
				int action)
{
	/* Dynamically enable/disable speaker amps before and after playback */
	if (action == HDA_GEN_PCM_ACT_OPEN)
		alc298_samsung_v2_enable_amps(codec);
	if (action == HDA_GEN_PCM_ACT_CLOSE)
		alc298_samsung_v2_disable_amps(codec);
}

static void alc298_samsung_v2_init_amps(struct hda_codec *codec,
				int num_speaker_amps)
{
	struct alc_spec *spec = codec->spec;
	int i, j;

	/* Set spec's num_speaker_amps before doing anything else */
	spec->num_speaker_amps = num_speaker_amps;

	/* Disable speaker amps before init to prevent any physical damage */
	alc298_samsung_v2_disable_amps(codec);

	/* Initialize the speaker amps */
	for (i = 0; i < spec->num_speaker_amps; i++) {
		alc_write_coef_idx(codec, 0x22, alc298_samsung_v2_amp_desc_tbl[i].nid);
		for (j = 0; j < alc298_samsung_v2_amp_desc_tbl[i].init_seq_size; j++) {
			alc298_samsung_write_coef_pack(codec,
					alc298_samsung_v2_amp_desc_tbl[i].init_seq[j]);
		}
		alc_write_coef_idx(codec, 0x89, 0x0);
		codec_dbg(codec, "alc298_samsung_v2: Initialized speaker amp 0x%02x\n",
				alc298_samsung_v2_amp_desc_tbl[i].nid);
	}

	/* register hook to enable speaker amps only when they are needed */
	spec->gen.pcm_playback_hook = alc298_samsung_v2_playback_hook;
}

static void alc298_fixup_samsung_amp_v2_2_amps(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PROBE)
		alc298_samsung_v2_init_amps(codec, 2);
}

static void alc298_fixup_samsung_amp_v2_4_amps(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PROBE)
		alc298_samsung_v2_init_amps(codec, 4);
}

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

static void alc269_fixup_hp_line1_mic1_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	alc269_fixup_hp_mute_led_micx(codec, fix, action, 0x1a);
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->cap_mute_led_nid = 0x18;
		snd_hda_gen_add_micmute_led_cdev(codec, vref_micmute_led_set);
	}
}

static void alc233_fixup_lenovo_low_en_micmute_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->micmute_led_polarity = 1;
	alc233_fixup_lenovo_line2_mic_hotkey(codec, fix, action);
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
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
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

static void alc_fixup_no_shutup(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->no_shutup_pins = 1;
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

static void alc295_fixup_asus_dacs(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	static const hda_nid_t preferred_pairs[] = {
		0x17, 0x02, 0x21, 0x03, 0
	};
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->gen.preferred_dacs = preferred_pairs;
}

static void alc271_hp_gate_mic_jack(struct hda_codec *codec,
				    const struct hda_fixup *fix,
				    int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		int mic_pin = alc_find_ext_mic_pin(codec);
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

/* disable DAC3 (0x06) selection on NID 0x15 - share Speaker/Bass Speaker DAC 0x03 */
static void alc294_fixup_bass_speaker_15(struct hda_codec *codec,
					 const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		static const hda_nid_t conn[] = { 0x02, 0x03 };
		snd_hda_override_conn_list(codec, 0x15, ARRAY_SIZE(conn), conn);
		snd_hda_gen_add_micmute_led_cdev(codec, NULL);
	}
}

/* Hook to update amp GPIO4 for automute */
static void alc280_hp_gpio4_automute_hook(struct hda_codec *codec,
					  struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_gen_hp_automute(codec, jack);
	/* mute_led_polarity is set to 0, so we pass inverted value here */
	alc_update_gpio_led(codec, 0x10, spec->mute_led_polarity,
			    !spec->gen.hp_jack_present);
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

/* Quirk for Thinkpad X1 7th and 8th Gen
 * The following fixed routing needed
 * DAC1 (NID 0x02) -> Speaker (NID 0x14); some eq applied secretly
 * DAC2 (NID 0x03) -> Bass (NID 0x17) & Headphone (NID 0x21); sharing a DAC
 * DAC3 (NID 0x06) -> Unused, due to the lack of volume amp
 */
static void alc285_fixup_thinkpad_x1_gen7(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	static const hda_nid_t conn[] = { 0x02, 0x03 }; /* exclude 0x06 */
	static const hda_nid_t preferred_pairs[] = {
		0x14, 0x02, 0x17, 0x03, 0x21, 0x03, 0
	};
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		spec->gen.preferred_dacs = preferred_pairs;
		break;
	case HDA_FIXUP_ACT_BUILD:
		/* The generic parser creates somewhat unintuitive volume ctls
		 * with the fixed routing above, and the shared DAC2 may be
		 * confusing for PA.
		 * Rename those to unique names so that PA doesn't touch them
		 * and use only Master volume.
		 */
		rename_ctl(codec, "Front Playback Volume", "DAC1 Playback Volume");
		rename_ctl(codec, "Bass Speaker Playback Volume", "DAC2 Playback Volume");
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

/* avoid DAC 0x06 for speaker switch 0x17; it has no volume control */
static void alc274_fixup_hp_aio_bind_dacs(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static const hda_nid_t conn[] = { 0x02, 0x03 }; /* exclude 0x06 */
	/* The speaker is routed to the Node 0x06 by a mistake, thus the
	 * speaker's volume can't be adjusted since the node doesn't have
	 * Amp-out capability. Assure the speaker and lineout pin to be
	 * coupled with DAC NID 0x02.
	 */
	static const hda_nid_t preferred_pairs[] = {
		0x16, 0x02, 0x17, 0x02, 0x21, 0x03, 0
	};
	struct alc_spec *spec = codec->spec;

	snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
	spec->gen.preferred_dacs = preferred_pairs;
}

/* avoid DAC 0x06 for bass speaker 0x17; it has no volume control */
static void alc289_fixup_asus_ga401(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static const hda_nid_t preferred_pairs[] = {
		0x14, 0x02, 0x17, 0x02, 0x21, 0x03, 0
	};
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->gen.preferred_dacs = preferred_pairs;
}

/* The DAC of NID 0x3 will introduce click/pop noise on headphones, so invalidate it */
static void alc285_fixup_invalidate_dacs(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_override_wcaps(codec, 0x03, 0);
}

static void alc_combo_jack_hp_jd_restart(struct hda_codec *codec)
{
	switch (codec->core.vendor_id) {
	case 0x10ec0274:
	case 0x10ec0294:
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_update_coef_idx(codec, 0x4a, 0x8000, 1 << 15); /* Reset HP JD */
		alc_update_coef_idx(codec, 0x4a, 0x8000, 0 << 15);
		break;
	case 0x10ec0230:
	case 0x10ec0235:
	case 0x10ec0236:
	case 0x10ec0255:
	case 0x10ec0256:
	case 0x10ec0257:
	case 0x19e58326:
		alc_update_coef_idx(codec, 0x1b, 0x8000, 1 << 15); /* Reset HP JD */
		alc_update_coef_idx(codec, 0x1b, 0x8000, 0 << 15);
		break;
	}
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
		alc_combo_jack_hp_jd_restart(codec);
		break;
	}
}

static void alc256_fixup_chromebook(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		if (codec->core.subsystem_id == 0x10280d76)
			spec->gen.suppress_auto_mute = 0;
		else
			spec->gen.suppress_auto_mute = 1;
		spec->gen.suppress_auto_mic = 1;
		spec->en_3kpull_low = false;
		break;
	}
}

static void alc_fixup_disable_mic_vref(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_codec_set_pin_target(codec, 0x19, PIN_VREFHIZ);
}


static void alc294_gx502_toggle_output(struct hda_codec *codec,
					struct hda_jack_callback *cb)
{
	/* The Windows driver sets the codec up in a very different way where
	 * it appears to leave 0x10 = 0x8a20 set. For Linux we need to toggle it
	 */
	if (snd_hda_jack_detect_state(codec, 0x21) == HDA_JACK_PRESENT)
		alc_write_coef_idx(codec, 0x10, 0x8a20);
	else
		alc_write_coef_idx(codec, 0x10, 0x0a20);
}

static void alc294_fixup_gx502_hp(struct hda_codec *codec,
					const struct hda_fixup *fix, int action)
{
	/* Pin 0x21: headphones/headset mic */
	if (!is_jack_detectable(codec, 0x21))
		return;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_jack_detect_enable_callback(codec, 0x21,
				alc294_gx502_toggle_output);
		break;
	case HDA_FIXUP_ACT_INIT:
		/* Make sure to start in a correct state, i.e. if
		 * headphones have been plugged in before powering up the system
		 */
		alc294_gx502_toggle_output(codec, NULL);
		break;
	}
}

static void alc294_gu502_toggle_output(struct hda_codec *codec,
				       struct hda_jack_callback *cb)
{
	/* Windows sets 0x10 to 0x8420 for Node 0x20 which is
	 * responsible from changes between speakers and headphones
	 */
	if (snd_hda_jack_detect_state(codec, 0x21) == HDA_JACK_PRESENT)
		alc_write_coef_idx(codec, 0x10, 0x8420);
	else
		alc_write_coef_idx(codec, 0x10, 0x0a20);
}

static void alc294_fixup_gu502_hp(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (!is_jack_detectable(codec, 0x21))
		return;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_jack_detect_enable_callback(codec, 0x21,
				alc294_gu502_toggle_output);
		break;
	case HDA_FIXUP_ACT_INIT:
		alc294_gu502_toggle_output(codec, NULL);
		break;
	}
}

static void  alc285_fixup_hp_gpio_amp_init(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_INIT)
		return;

	msleep(100);
	alc_write_coef_idx(codec, 0x65, 0x0);
}

static void alc274_fixup_hp_headset_mic(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	switch (action) {
	case HDA_FIXUP_ACT_INIT:
		alc_combo_jack_hp_jd_restart(codec);
		break;
	}
}

static void alc_fixup_no_int_mic(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* Mic RING SLEEVE swap for combo jack */
		alc_update_coef_idx(codec, 0x45, 0xf<<12 | 1<<10, 5<<12);
		spec->no_internal_mic_pin = true;
		break;
	case HDA_FIXUP_ACT_INIT:
		alc_combo_jack_hp_jd_restart(codec);
		break;
	}
}

/* GPIO1 = amplifier on/off
 * GPIO3 = mic mute LED
 */
static void alc285_fixup_hp_spectre_x360_eb1(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	static const hda_nid_t conn[] = { 0x02 };

	struct alc_spec *spec = codec->spec;
	static const struct hda_pintbl pincfgs[] = {
		{ 0x14, 0x90170110 },  /* front/high speakers */
		{ 0x17, 0x90170130 },  /* back/bass speakers */
		{ }
	};

	//enable micmute led
	alc_fixup_hp_gpio_led(codec, action, 0x00, 0x04);

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->micmute_led_polarity = 1;
		/* needed for amp of back speakers */
		spec->gpio_mask |= 0x01;
		spec->gpio_dir |= 0x01;
		snd_hda_apply_pincfgs(codec, pincfgs);
		/* share DAC to have unified volume control */
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		break;
	case HDA_FIXUP_ACT_INIT:
		/* need to toggle GPIO to enable the amp of back speakers */
		alc_update_gpio_data(codec, 0x01, true);
		msleep(100);
		alc_update_gpio_data(codec, 0x01, false);
		break;
	}
}

/* GPIO1 = amplifier on/off */
static void alc285_fixup_hp_spectre_x360_df1(struct hda_codec *codec,
					     const struct hda_fixup *fix,
					     int action)
{
	struct alc_spec *spec = codec->spec;
	static const hda_nid_t conn[] = { 0x02 };
	static const struct hda_pintbl pincfgs[] = {
		{ 0x14, 0x90170110 },  /* front/high speakers */
		{ 0x17, 0x90170130 },  /* back/bass speakers */
		{ }
	};

	// enable mute led
	alc285_fixup_hp_mute_led_coefbit(codec, fix, action);

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* needed for amp of back speakers */
		spec->gpio_mask |= 0x01;
		spec->gpio_dir |= 0x01;
		snd_hda_apply_pincfgs(codec, pincfgs);
		/* share DAC to have unified volume control */
		snd_hda_override_conn_list(codec, 0x14, ARRAY_SIZE(conn), conn);
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		break;
	case HDA_FIXUP_ACT_INIT:
		/* need to toggle GPIO to enable the amp of back speakers */
		alc_update_gpio_data(codec, 0x01, true);
		msleep(100);
		alc_update_gpio_data(codec, 0x01, false);
		break;
	}
}

static void alc285_fixup_hp_spectre_x360(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	static const hda_nid_t conn[] = { 0x02 };
	static const struct hda_pintbl pincfgs[] = {
		{ 0x14, 0x90170110 },  /* rear speaker */
		{ }
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		/* force front speaker to DAC1 */
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		break;
	}
}

static void alc285_fixup_hp_envy_x360(struct hda_codec *codec,
				      const struct hda_fixup *fix,
				      int action)
{
	static const struct coef_fw coefs[] = {
		WRITE_COEF(0x08, 0x6a0c), WRITE_COEF(0x0d, 0xa023),
		WRITE_COEF(0x10, 0x0320), WRITE_COEF(0x1a, 0x8c03),
		WRITE_COEF(0x25, 0x1800), WRITE_COEF(0x26, 0x003a),
		WRITE_COEF(0x28, 0x1dfe), WRITE_COEF(0x29, 0xb014),
		WRITE_COEF(0x2b, 0x1dfe), WRITE_COEF(0x37, 0xfe15),
		WRITE_COEF(0x38, 0x7909), WRITE_COEF(0x45, 0xd489),
		WRITE_COEF(0x46, 0x00f4), WRITE_COEF(0x4a, 0x21e0),
		WRITE_COEF(0x66, 0x03f0), WRITE_COEF(0x67, 0x1000),
		WRITE_COEF(0x6e, 0x1005), { }
	};

	static const struct hda_pintbl pincfgs[] = {
		{ 0x12, 0xb7a60130 },  /* Internal microphone*/
		{ 0x14, 0x90170150 },  /* B&O soundbar speakers */
		{ 0x17, 0x90170153 },  /* Side speakers */
		{ 0x19, 0x03a11040 },  /* Headset microphone */
		{ }
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);

		/* Fixes volume control problem for side speakers */
		alc295_fixup_disable_dac3(codec, fix, action);

		/* Fixes no sound from headset speaker */
		snd_hda_codec_amp_stereo(codec, 0x21, HDA_OUTPUT, 0, -1, 0);

		/* Auto-enable headset mic when plugged */
		snd_hda_jack_set_gating_jack(codec, 0x19, 0x21);

		/* Headset mic volume enhancement */
		snd_hda_codec_set_pin_target(codec, 0x19, PIN_VREF50);
		break;
	case HDA_FIXUP_ACT_INIT:
		alc_process_coef_fw(codec, coefs);
		break;
	case HDA_FIXUP_ACT_BUILD:
		rename_ctl(codec, "Bass Speaker Playback Volume",
			   "B&O-Tuned Playback Volume");
		rename_ctl(codec, "Front Playback Switch",
			   "B&O Soundbar Playback Switch");
		rename_ctl(codec, "Bass Speaker Playback Switch",
			   "Side Speaker Playback Switch");
		break;
	}
}

static void alc285_fixup_hp_beep(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		codec->beep_just_power_on = true;
	} else  if (action == HDA_FIXUP_ACT_INIT) {
#ifdef CONFIG_SND_HDA_INPUT_BEEP
		/*
		 * Just enable loopback to internal speaker and headphone jack.
		 * Disable amplification to get about the same beep volume as
		 * was on pure BIOS setup before loading the driver.
		 */
		alc_update_coef_idx(codec, 0x36, 0x7070, BIT(13));

		snd_hda_enable_beep_device(codec, 1);

#if !IS_ENABLED(CONFIG_INPUT_PCSPKR)
		dev_warn_once(hda_codec_dev(codec),
			      "enable CONFIG_INPUT_PCSPKR to get PC beeps\n");
#endif
#endif
	}
}

/* for hda_fixup_thinkpad_acpi() */
#include "../helpers/thinkpad.c"

static void alc_fixup_thinkpad_acpi(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	alc_fixup_no_shutup(codec, fix, action); /* reduce click noise */
	hda_fixup_thinkpad_acpi(codec, fix, action);
}

/* for hda_fixup_ideapad_acpi() */
#include "../helpers/ideapad_hotkey_led.c"

static void alc_fixup_ideapad_acpi(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	hda_fixup_ideapad_acpi(codec, fix, action);
}

/* Fixup for Lenovo Legion 15IMHg05 speaker output on headset removal. */
static void alc287_fixup_legion_15imhg05_speakers(struct hda_codec *codec,
						  const struct hda_fixup *fix,
						  int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gen.suppress_auto_mute = 1;
		break;
	}
}

static void comp_acpi_device_notify(acpi_handle handle, u32 event, void *data)
{
	struct hda_codec *cdc = data;
	struct alc_spec *spec = cdc->spec;

	codec_info(cdc, "ACPI Notification %d\n", event);

	hda_component_acpi_device_notify(&spec->comps, handle, event, data);
}

static int comp_bind(struct device *dev)
{
	struct hda_codec *cdc = dev_to_hda_codec(dev);
	struct alc_spec *spec = cdc->spec;
	int ret;

	ret = hda_component_manager_bind(cdc, &spec->comps);
	if (ret)
		return ret;

	return hda_component_manager_bind_acpi_notifications(cdc,
							     &spec->comps,
							     comp_acpi_device_notify, cdc);
}

static void comp_unbind(struct device *dev)
{
	struct hda_codec *cdc = dev_to_hda_codec(dev);
	struct alc_spec *spec = cdc->spec;

	hda_component_manager_unbind_acpi_notifications(cdc, &spec->comps, comp_acpi_device_notify);
	hda_component_manager_unbind(cdc, &spec->comps);
}

static const struct component_master_ops comp_master_ops = {
	.bind = comp_bind,
	.unbind = comp_unbind,
};

static void comp_generic_playback_hook(struct hda_pcm_stream *hinfo, struct hda_codec *cdc,
				       struct snd_pcm_substream *sub, int action)
{
	struct alc_spec *spec = cdc->spec;

	hda_component_manager_playback_hook(&spec->comps, action);
}

static void comp_generic_fixup(struct hda_codec *cdc, int action, const char *bus,
			       const char *hid, const char *match_str, int count)
{
	struct alc_spec *spec = cdc->spec;
	int ret;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		ret = hda_component_manager_init(cdc, &spec->comps, count, bus, hid,
						 match_str, &comp_master_ops);
		if (ret)
			return;

		spec->gen.pcm_playback_hook = comp_generic_playback_hook;
		break;
	case HDA_FIXUP_ACT_FREE:
		hda_component_manager_free(&spec->comps, &comp_master_ops);
		break;
	}
}

static void find_cirrus_companion_amps(struct hda_codec *cdc)
{
	struct device *dev = hda_codec_dev(cdc);
	struct acpi_device *adev;
	struct fwnode_handle *fwnode __free(fwnode_handle) = NULL;
	const char *bus = NULL;
	static const struct {
		const char *hid;
		const char *name;
	} acpi_ids[] = {{ "CSC3554", "cs35l54-hda" },
			{ "CSC3556", "cs35l56-hda" },
			{ "CSC3557", "cs35l57-hda" }};
	char *match;
	int i, count = 0, count_devindex = 0;

	for (i = 0; i < ARRAY_SIZE(acpi_ids); ++i) {
		adev = acpi_dev_get_first_match_dev(acpi_ids[i].hid, NULL, -1);
		if (adev)
			break;
	}
	if (!adev) {
		codec_dbg(cdc, "Did not find ACPI entry for a Cirrus Amp\n");
		return;
	}

	count = i2c_acpi_client_count(adev);
	if (count > 0) {
		bus = "i2c";
	} else {
		count = acpi_spi_count_resources(adev);
		if (count > 0)
			bus = "spi";
	}

	fwnode = fwnode_handle_get(acpi_fwnode_handle(adev));
	acpi_dev_put(adev);

	if (!bus) {
		codec_err(cdc, "Did not find any buses for %s\n", acpi_ids[i].hid);
		return;
	}

	if (!fwnode) {
		codec_err(cdc, "Could not get fwnode for %s\n", acpi_ids[i].hid);
		return;
	}

	/*
	 * When available the cirrus,dev-index property is an accurate
	 * count of the amps in a system and is used in preference to
	 * the count of bus devices that can contain additional address
	 * alias entries.
	 */
	count_devindex = fwnode_property_count_u32(fwnode, "cirrus,dev-index");
	if (count_devindex > 0)
		count = count_devindex;

	match = devm_kasprintf(dev, GFP_KERNEL, "-%%s:00-%s.%%d", acpi_ids[i].name);
	if (!match)
		return;
	codec_info(cdc, "Found %d %s on %s (%s)\n", count, acpi_ids[i].hid, bus, match);
	comp_generic_fixup(cdc, HDA_FIXUP_ACT_PRE_PROBE, bus, acpi_ids[i].hid, match, count);
}

static void cs35l41_fixup_i2c_two(struct hda_codec *cdc, const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(cdc, action, "i2c", "CSC3551", "-%s:00-cs35l41-hda.%d", 2);
}

static void cs35l41_fixup_i2c_four(struct hda_codec *cdc, const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(cdc, action, "i2c", "CSC3551", "-%s:00-cs35l41-hda.%d", 4);
}

static void cs35l41_fixup_spi_two(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(codec, action, "spi", "CSC3551", "-%s:00-cs35l41-hda.%d", 2);
}

static void cs35l41_fixup_spi_one(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(codec, action, "spi", "CSC3551", "-%s:00-cs35l41-hda.%d", 1);
}

static void cs35l41_fixup_spi_four(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(codec, action, "spi", "CSC3551", "-%s:00-cs35l41-hda.%d", 4);
}

static void alc287_fixup_legion_16achg6_speakers(struct hda_codec *cdc, const struct hda_fixup *fix,
						 int action)
{
	comp_generic_fixup(cdc, action, "i2c", "CLSA0100", "-%s:00-cs35l41-hda.%d", 2);
}

static void alc287_fixup_legion_16ithg6_speakers(struct hda_codec *cdc, const struct hda_fixup *fix,
						 int action)
{
	comp_generic_fixup(cdc, action, "i2c", "CLSA0101", "-%s:00-cs35l41-hda.%d", 2);
}

static void alc285_fixup_asus_ga403u(struct hda_codec *cdc, const struct hda_fixup *fix, int action)
{
	/*
	 * The same SSID has been re-used in different hardware, they have
	 * different codecs and the newer GA403U has a ALC285.
	 */
	if (cdc->core.vendor_id != 0x10ec0285)
		alc_fixup_inv_dmic(cdc, fix, action);
}

static void tas2781_fixup_tias_i2c(struct hda_codec *cdc,
	const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(cdc, action, "i2c", "TIAS2781", "-%s:00", 1);
}

static void tas2781_fixup_spi(struct hda_codec *cdc, const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(cdc, action, "spi", "TXNW2781", "-%s:00-tas2781-hda.%d", 2);
}

static void tas2781_fixup_txnw_i2c(struct hda_codec *cdc,
	const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(cdc, action, "i2c", "TXNW2781", "-%s:00-tas2781-hda.%d", 1);
}

static void yoga7_14arb7_fixup_i2c(struct hda_codec *cdc,
	const struct hda_fixup *fix, int action)
{
	comp_generic_fixup(cdc, action, "i2c", "INT8866", "-%s:00", 1);
}

static void alc256_fixup_acer_sfg16_micmute_led(struct hda_codec *codec,
	const struct hda_fixup *fix, int action)
{
	alc_fixup_hp_gpio_led(codec, action, 0, 0x04);
}


/* for alc295_fixup_hp_top_speakers */
#include "../helpers/hp_x360.c"

/* for alc285_fixup_ideapad_s740_coef() */
#include "../helpers/ideapad_s740.c"

static const struct coef_fw alc256_fixup_set_coef_defaults_coefs[] = {
	WRITE_COEF(0x10, 0x0020), WRITE_COEF(0x24, 0x0000),
	WRITE_COEF(0x26, 0x0000), WRITE_COEF(0x29, 0x3000),
	WRITE_COEF(0x37, 0xfe05), WRITE_COEF(0x45, 0x5089),
	{}
};

static void alc256_fixup_set_coef_defaults(struct hda_codec *codec,
					   const struct hda_fixup *fix,
					   int action)
{
	/*
	 * A certain other OS sets these coeffs to different values. On at least
	 * one TongFang barebone these settings might survive even a cold
	 * reboot. So to restore a clean slate the values are explicitly reset
	 * to default here. Without this, the external microphone is always in a
	 * plugged-in state, while the internal microphone is always in an
	 * unplugged state, breaking the ability to use the internal microphone.
	 */
	alc_process_coef_fw(codec, alc256_fixup_set_coef_defaults_coefs);
}

static const struct coef_fw alc233_fixup_no_audio_jack_coefs[] = {
	WRITE_COEF(0x1a, 0x9003), WRITE_COEF(0x1b, 0x0e2b), WRITE_COEF(0x37, 0xfe06),
	WRITE_COEF(0x38, 0x4981), WRITE_COEF(0x45, 0xd489), WRITE_COEF(0x46, 0x0074),
	WRITE_COEF(0x49, 0x0149),
	{}
};

static void alc233_fixup_no_audio_jack(struct hda_codec *codec,
				       const struct hda_fixup *fix,
				       int action)
{
	/*
	 * The audio jack input and output is not detected on the ASRock NUC Box
	 * 1100 series when cold booting without this fix. Warm rebooting from a
	 * certain other OS makes the audio functional, as COEF settings are
	 * preserved in this case. This fix sets these altered COEF values as
	 * the default.
	 */
	alc_process_coef_fw(codec, alc233_fixup_no_audio_jack_coefs);
}

static void alc256_fixup_mic_no_presence_and_resume(struct hda_codec *codec,
						    const struct hda_fixup *fix,
						    int action)
{
	/*
	 * The Clevo NJ51CU comes either with the ALC293 or the ALC256 codec,
	 * but uses the 0x8686 subproduct id in both cases. The ALC256 codec
	 * needs an additional quirk for sound working after suspend and resume.
	 */
	if (codec->core.vendor_id == 0x10ec0256) {
		alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		snd_hda_codec_set_pincfg(codec, 0x19, 0x04a11120);
	} else {
		snd_hda_codec_set_pincfg(codec, 0x1a, 0x04a1113c);
	}
}

static void alc256_decrease_headphone_amp_val(struct hda_codec *codec,
					      const struct hda_fixup *fix, int action)
{
	u32 caps;
	u8 nsteps, offs;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	caps = query_amp_caps(codec, 0x3, HDA_OUTPUT);
	nsteps = ((caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT) - 10;
	offs = ((caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT) - 10;
	caps &= ~AC_AMPCAP_NUM_STEPS & ~AC_AMPCAP_OFFSET;
	caps |= (nsteps << AC_AMPCAP_NUM_STEPS_SHIFT) | (offs << AC_AMPCAP_OFFSET_SHIFT);

	if (snd_hda_override_amp_caps(codec, 0x3, HDA_OUTPUT, caps))
		codec_warn(codec, "failed to override amp caps for NID 0x3\n");
}

static void alc_fixup_dell4_mic_no_presence_quiet(struct hda_codec *codec,
						  const struct hda_fixup *fix,
						  int action)
{
	struct alc_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->gen.input_mux;
	int i;

	alc269_fixup_limit_int_mic_boost(codec, fix, action);

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/**
		 * Set the vref of pin 0x19 (Headset Mic) and pin 0x1b (Headphone Mic)
		 * to Hi-Z to avoid pop noises at startup and when plugging and
		 * unplugging headphones.
		 */
		snd_hda_codec_set_pin_target(codec, 0x19, PIN_VREFHIZ);
		snd_hda_codec_set_pin_target(codec, 0x1b, PIN_VREFHIZ);
		break;
	case HDA_FIXUP_ACT_PROBE:
		/**
		 * Make the internal mic (0x12) the default input source to
		 * prevent pop noises on cold boot.
		 */
		for (i = 0; i < imux->num_items; i++) {
			if (spec->gen.imux_pins[i] == 0x12) {
				spec->gen.cur_mux[0] = i;
				break;
			}
		}
		break;
	}
}

static void alc287_fixup_yoga9_14iap7_bass_spk_pin(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	/*
	 * The Pin Complex 0x17 for the bass speakers is wrongly reported as
	 * unconnected.
	 */
	static const struct hda_pintbl pincfgs[] = {
		{ 0x17, 0x90170121 },
		{ }
	};
	/*
	 * Avoid DAC 0x06 and 0x08, as they have no volume controls.
	 * DAC 0x02 and 0x03 would be fine.
	 */
	static const hda_nid_t conn[] = { 0x02, 0x03 };
	/*
	 * Prefer both speakerbar (0x14) and bass speakers (0x17) connected to DAC 0x02.
	 * Headphones (0x21) are connected to DAC 0x03.
	 */
	static const hda_nid_t preferred_pairs[] = {
		0x14, 0x02,
		0x17, 0x02,
		0x21, 0x03,
		0
	};
	struct alc_spec *spec = codec->spec;

	/* Support Audio mute LED and Mic mute LED on keyboard */
	hda_fixup_ideapad_acpi(codec, fix, action);

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		spec->gen.preferred_dacs = preferred_pairs;
		break;
	}
}

static void alc295_fixup_dell_inspiron_top_speakers(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	static const struct hda_pintbl pincfgs[] = {
		{ 0x14, 0x90170151 },
		{ 0x17, 0x90170150 },
		{ }
	};
	static const hda_nid_t conn[] = { 0x02, 0x03 };
	static const hda_nid_t preferred_pairs[] = {
		0x14, 0x02,
		0x17, 0x03,
		0x21, 0x02,
		0
	};
	struct alc_spec *spec = codec->spec;

	alc_fixup_no_shutup(codec, fix, action);

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		spec->gen.preferred_dacs = preferred_pairs;
		break;
	}
}

/* Forcibly assign NID 0x03 to HP while NID 0x02 to SPK */
static void alc287_fixup_bind_dacs(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const hda_nid_t conn[] = { 0x02, 0x03 }; /* exclude 0x06 */
	static const hda_nid_t preferred_pairs[] = {
		0x17, 0x02, 0x21, 0x03, 0
	};

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
	spec->gen.preferred_dacs = preferred_pairs;
	spec->gen.auto_mute_via_amp = 1;
	if (spec->gen.autocfg.speaker_pins[0] != 0x14) {
		snd_hda_codec_write_cache(codec, 0x14, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
					0x0); /* Make sure 0x14 was disable */
	}
}

/* Fix none verb table of Headset Mic pin */
static void alc2xx_fixup_headset_mic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_pintbl pincfgs[] = {
		{ 0x19, 0x03a1103c },
		{ }
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		alc_update_coef_idx(codec, 0x45, 0xf<<12 | 1<<10, 5<<12);
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		break;
	}
}

static void alc245_fixup_hp_spectre_x360_eu0xxx(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	/*
	 * The Pin Complex 0x14 for the treble speakers is wrongly reported as
	 * unconnected.
	 * The Pin Complex 0x17 for the bass speakers has the lowest association
	 * and sequence values so shift it up a bit to squeeze 0x14 in.
	 */
	static const struct hda_pintbl pincfgs[] = {
		{ 0x14, 0x90170110 }, // top/treble
		{ 0x17, 0x90170111 }, // bottom/bass
		{ }
	};

	/*
	 * Force DAC 0x02 for the bass speakers 0x17.
	 */
	static const hda_nid_t conn[] = { 0x02 };

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		break;
	}

	cs35l41_fixup_i2c_two(codec, fix, action);
	alc245_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc245_fixup_hp_gpio_led(codec, fix, action);
}

/* some changes for Spectre x360 16, 2024 model */
static void alc245_fixup_hp_spectre_x360_16_aa0xxx(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	/*
	 * The Pin Complex 0x14 for the treble speakers is wrongly reported as
	 * unconnected.
	 * The Pin Complex 0x17 for the bass speakers has the lowest association
	 * and sequence values so shift it up a bit to squeeze 0x14 in.
	 */
	struct alc_spec *spec = codec->spec;
	static const struct hda_pintbl pincfgs[] = {
		{ 0x14, 0x90170110 }, // top/treble
		{ 0x17, 0x90170111 }, // bottom/bass
		{ }
	};

	/*
	 * Force DAC 0x02 for the bass speakers 0x17.
	 */
	static const hda_nid_t conn[] = { 0x02 };

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* needed for amp of back speakers */
		spec->gpio_mask |= 0x01;
		spec->gpio_dir |= 0x01;
		snd_hda_apply_pincfgs(codec, pincfgs);
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		break;
	case HDA_FIXUP_ACT_INIT:
		/* need to toggle GPIO to enable the amp of back speakers */
		alc_update_gpio_data(codec, 0x01, true);
		msleep(100);
		alc_update_gpio_data(codec, 0x01, false);
		break;
	}

	cs35l41_fixup_i2c_two(codec, fix, action);
	alc245_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc245_fixup_hp_gpio_led(codec, fix, action);
}

static void alc245_fixup_hp_zbook_firefly_g12a(struct hda_codec *codec,
					  const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const hda_nid_t conn[] = { 0x02 };

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gen.auto_mute_via_amp = 1;
		snd_hda_override_conn_list(codec, 0x17, ARRAY_SIZE(conn), conn);
		break;
	}

	cs35l41_fixup_i2c_two(codec, fix, action);
	alc245_fixup_hp_mute_led_coefbit(codec, fix, action);
	alc285_fixup_hp_coef_micmute_led(codec, fix, action);
}

/*
 * ALC287 PCM hooks
 */
static void alc287_alc1318_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream,
				   int action)
{
	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		alc_write_coefex_idx(codec, 0x5a, 0x00, 0x954f); /* write gpio3 to high */
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		alc_write_coefex_idx(codec, 0x5a, 0x00, 0x554f); /* write gpio3 as default value */
		break;
	}
}

static void alc287_s4_power_gpio3_default(struct hda_codec *codec)
{
	if (is_s4_suspend(codec)) {
		alc_write_coefex_idx(codec, 0x5a, 0x00, 0x554f); /* write gpio3 as default value */
	}
}

static void alc287_fixup_lenovo_thinkpad_with_alc1318(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct coef_fw coefs[] = {
		WRITE_COEF(0x24, 0x0013), WRITE_COEF(0x25, 0x0000), WRITE_COEF(0x26, 0xC300),
		WRITE_COEF(0x28, 0x0001), WRITE_COEF(0x29, 0xb023),
		WRITE_COEF(0x24, 0x0013), WRITE_COEF(0x25, 0x0000), WRITE_COEF(0x26, 0xC301),
		WRITE_COEF(0x28, 0x0001), WRITE_COEF(0x29, 0xb023),
	};

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	alc_update_coef_idx(codec, 0x10, 1<<11, 1<<11);
	alc_process_coef_fw(codec, coefs);
	spec->power_hook = alc287_s4_power_gpio3_default;
	spec->gen.pcm_playback_hook = alc287_alc1318_playback_pcm_hook;
}

/*
 * Clear COEF 0x0d (PCBEEP passthrough) bit 0x40 where BIOS sets it wrongly
 * at PM resume
 */
static void alc283_fixup_dell_hp_resume(struct hda_codec *codec,
					const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_INIT)
		alc_write_coef_idx(codec, 0xd, 0x2800);
}

enum {
	ALC269_FIXUP_GPIO2,
	ALC269_FIXUP_SONY_VAIO,
	ALC275_FIXUP_SONY_VAIO_GPIO2,
	ALC269_FIXUP_DELL_M101Z,
	ALC269_FIXUP_SKU_IGNORE,
	ALC269_FIXUP_ASUS_G73JW,
	ALC269_FIXUP_ASUS_N7601ZM_PINS,
	ALC269_FIXUP_ASUS_N7601ZM,
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
	ALC269_FIXUP_DELL1_LIMIT_INT_MIC_BOOST,
	ALC269_FIXUP_DELL2_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL3_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL4_MIC_NO_PRESENCE_QUIET,
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
	ALC269VB_FIXUP_ASUS_MIC_NO_PRESENCE,
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
	ALC295_FIXUP_HP_MUTE_LED_COEFBIT11,
	ALC269_FIXUP_THINKPAD_ACPI,
	ALC269_FIXUP_LENOVO_XPAD_ACPI,
	ALC269_FIXUP_DMIC_THINKPAD_ACPI,
	ALC269VB_FIXUP_INFINIX_ZERO_BOOK_13,
	ALC269VC_FIXUP_INFINIX_Y4_MAX,
	ALC269VB_FIXUP_CHUWI_COREBOOK_XPRO,
	ALC255_FIXUP_ACER_MIC_NO_PRESENCE,
	ALC255_FIXUP_ASUS_MIC_NO_PRESENCE,
	ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC255_FIXUP_DELL1_LIMIT_INT_MIC_BOOST,
	ALC255_FIXUP_DELL2_MIC_NO_PRESENCE,
	ALC255_FIXUP_HEADSET_MODE,
	ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC,
	ALC293_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC292_FIXUP_TPT440_DOCK,
	ALC292_FIXUP_TPT440,
	ALC283_FIXUP_HEADSET_MIC,
	ALC255_FIXUP_MIC_MUTE_LED,
	ALC282_FIXUP_ASPIRE_V5_PINS,
	ALC269VB_FIXUP_ASPIRE_E1_COEF,
	ALC280_FIXUP_HP_GPIO4,
	ALC286_FIXUP_HP_GPIO_LED,
	ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY,
	ALC280_FIXUP_HP_DOCK_PINS,
	ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED,
	ALC280_FIXUP_HP_9480M,
	ALC245_FIXUP_HP_X360_AMP,
	ALC285_FIXUP_HP_SPECTRE_X360_EB1,
	ALC285_FIXUP_HP_SPECTRE_X360_DF1,
	ALC285_FIXUP_HP_ENVY_X360,
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
	ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED,
	ALC255_FIXUP_DELL_SPK_NOISE,
	ALC225_FIXUP_DISABLE_MIC_VREF,
	ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC295_FIXUP_DISABLE_DAC3,
	ALC285_FIXUP_SPEAKER2_TO_DAC1,
	ALC285_FIXUP_ASUS_SPEAKER2_TO_DAC1,
	ALC285_FIXUP_ASUS_HEADSET_MIC,
	ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS,
	ALC285_FIXUP_ASUS_I2C_SPEAKER2_TO_DAC1,
	ALC285_FIXUP_ASUS_I2C_HEADSET_MIC,
	ALC280_FIXUP_HP_HEADSET_MIC,
	ALC221_FIXUP_HP_FRONT_MIC,
	ALC292_FIXUP_TPT460,
	ALC298_FIXUP_SPK_VOLUME,
	ALC298_FIXUP_LENOVO_SPK_VOLUME,
	ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER,
	ALC269_FIXUP_ATIV_BOOK_8,
	ALC221_FIXUP_HP_288PRO_MIC_NO_PRESENCE,
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
	ALC294_FIXUP_ASUS_I2C_HEADSET_MIC,
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
	ALC255_FIXUP_PREDATOR_SUBWOOFER,
	ALC299_FIXUP_PREDATOR_SPK,
	ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE,
	ALC289_FIXUP_DELL_SPK1,
	ALC289_FIXUP_DELL_SPK2,
	ALC289_FIXUP_DUAL_SPK,
	ALC289_FIXUP_RTK_AMP_DUAL_SPK,
	ALC294_FIXUP_SPK2_TO_DAC1,
	ALC294_FIXUP_ASUS_DUAL_SPK,
	ALC285_FIXUP_THINKPAD_X1_GEN7,
	ALC285_FIXUP_THINKPAD_HEADSET_JACK,
	ALC294_FIXUP_ASUS_ALLY,
	ALC294_FIXUP_ASUS_ALLY_PINS,
	ALC294_FIXUP_ASUS_ALLY_VERBS,
	ALC294_FIXUP_ASUS_ALLY_SPEAKER,
	ALC294_FIXUP_ASUS_HPE,
	ALC294_FIXUP_ASUS_COEF_1B,
	ALC294_FIXUP_ASUS_GX502_HP,
	ALC294_FIXUP_ASUS_GX502_PINS,
	ALC294_FIXUP_ASUS_GX502_VERBS,
	ALC294_FIXUP_ASUS_GU502_HP,
	ALC294_FIXUP_ASUS_GU502_PINS,
	ALC294_FIXUP_ASUS_GU502_VERBS,
	ALC294_FIXUP_ASUS_G513_PINS,
	ALC285_FIXUP_ASUS_G533Z_PINS,
	ALC285_FIXUP_HP_GPIO_LED,
	ALC285_FIXUP_HP_MUTE_LED,
	ALC285_FIXUP_HP_SPECTRE_X360_MUTE_LED,
	ALC285_FIXUP_HP_BEEP_MICMUTE_LED,
	ALC236_FIXUP_HP_MUTE_LED_COEFBIT2,
	ALC236_FIXUP_HP_GPIO_LED,
	ALC236_FIXUP_HP_MUTE_LED,
	ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF,
	ALC236_FIXUP_LENOVO_INV_DMIC,
	ALC298_FIXUP_SAMSUNG_AMP,
	ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS,
	ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS,
	ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET,
	ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET,
	ALC295_FIXUP_ASUS_MIC_NO_PRESENCE,
	ALC269VC_FIXUP_ACER_VCOPPERBOX_PINS,
	ALC269VC_FIXUP_ACER_HEADSET_MIC,
	ALC269VC_FIXUP_ACER_MIC_NO_PRESENCE,
	ALC289_FIXUP_ASUS_GA401,
	ALC289_FIXUP_ASUS_GA502,
	ALC256_FIXUP_ACER_MIC_NO_PRESENCE,
	ALC285_FIXUP_HP_GPIO_AMP_INIT,
	ALC269_FIXUP_CZC_B20,
	ALC269_FIXUP_CZC_TMI,
	ALC269_FIXUP_CZC_L101,
	ALC269_FIXUP_LEMOTE_A1802,
	ALC269_FIXUP_LEMOTE_A190X,
	ALC256_FIXUP_INTEL_NUC8_RUGGED,
	ALC233_FIXUP_INTEL_NUC8_DMIC,
	ALC233_FIXUP_INTEL_NUC8_BOOST,
	ALC256_FIXUP_INTEL_NUC10,
	ALC255_FIXUP_XIAOMI_HEADSET_MIC,
	ALC274_FIXUP_HP_MIC,
	ALC274_FIXUP_HP_HEADSET_MIC,
	ALC274_FIXUP_HP_ENVY_GPIO,
	ALC274_FIXUP_ASUS_ZEN_AIO_27,
	ALC256_FIXUP_ASUS_HPE,
	ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK,
	ALC287_FIXUP_HP_GPIO_LED,
	ALC256_FIXUP_HP_HEADSET_MIC,
	ALC245_FIXUP_HP_GPIO_LED,
	ALC236_FIXUP_DELL_AIO_HEADSET_MIC,
	ALC282_FIXUP_ACER_DISABLE_LINEOUT,
	ALC255_FIXUP_ACER_LIMIT_INT_MIC_BOOST,
	ALC256_FIXUP_ACER_HEADSET_MIC,
	ALC285_FIXUP_IDEAPAD_S740_COEF,
	ALC285_FIXUP_HP_LIMIT_INT_MIC_BOOST,
	ALC295_FIXUP_ASUS_DACS,
	ALC295_FIXUP_HP_OMEN,
	ALC285_FIXUP_HP_SPECTRE_X360,
	ALC287_FIXUP_IDEAPAD_BASS_SPK_AMP,
	ALC623_FIXUP_LENOVO_THINKSTATION_P340,
	ALC255_FIXUP_ACER_HEADPHONE_AND_MIC,
	ALC236_FIXUP_HP_LIMIT_INT_MIC_BOOST,
	ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS,
	ALC287_FIXUP_LEGION_15IMHG05_AUTOMUTE,
	ALC287_FIXUP_YOGA7_14ITL_SPEAKERS,
	ALC298_FIXUP_LENOVO_C940_DUET7,
	ALC287_FIXUP_13S_GEN2_SPEAKERS,
	ALC256_FIXUP_SET_COEF_DEFAULTS,
	ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE,
	ALC233_FIXUP_NO_AUDIO_JACK,
	ALC256_FIXUP_MIC_NO_PRESENCE_AND_RESUME,
	ALC285_FIXUP_LEGION_Y9000X_SPEAKERS,
	ALC285_FIXUP_LEGION_Y9000X_AUTOMUTE,
	ALC287_FIXUP_LEGION_16ACHG6,
	ALC287_FIXUP_CS35L41_I2C_2,
	ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED,
	ALC287_FIXUP_CS35L41_I2C_4,
	ALC245_FIXUP_CS35L41_SPI_1,
	ALC245_FIXUP_CS35L41_SPI_2,
	ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED,
	ALC245_FIXUP_CS35L41_SPI_4,
	ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED,
	ALC285_FIXUP_HP_SPEAKERS_MICMUTE_LED,
	ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE,
	ALC287_FIXUP_LEGION_16ITHG6,
	ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK,
	ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN,
	ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN,
	ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS,
	ALC236_FIXUP_DELL_DUAL_CODECS,
	ALC287_FIXUP_CS35L41_I2C_2_THINKPAD_ACPI,
	ALC287_FIXUP_TAS2781_I2C,
	ALC295_FIXUP_DELL_TAS2781_I2C,
	ALC245_FIXUP_TAS2781_SPI_2,
	ALC287_FIXUP_TXNW2781_I2C,
	ALC287_FIXUP_YOGA7_14ARB7_I2C,
	ALC245_FIXUP_HP_MUTE_LED_COEFBIT,
	ALC245_FIXUP_HP_MUTE_LED_V1_COEFBIT,
	ALC245_FIXUP_HP_X360_MUTE_LEDS,
	ALC287_FIXUP_THINKPAD_I2S_SPK,
	ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD,
	ALC2XX_FIXUP_HEADSET_MIC,
	ALC289_FIXUP_DELL_CS35L41_SPI_2,
	ALC294_FIXUP_CS35L41_I2C_2,
	ALC256_FIXUP_ACER_SFG16_MICMUTE_LED,
	ALC256_FIXUP_HEADPHONE_AMP_VOL,
	ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX,
	ALC245_FIXUP_HP_SPECTRE_X360_16_AA0XXX,
	ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A,
	ALC285_FIXUP_ASUS_GA403U,
	ALC285_FIXUP_ASUS_GA403U_HEADSET_MIC,
	ALC285_FIXUP_ASUS_GA403U_I2C_SPEAKER2_TO_DAC1,
	ALC285_FIXUP_ASUS_GU605_SPI_2_HEADSET_MIC,
	ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1,
	ALC287_FIXUP_LENOVO_THKPAD_WH_ALC1318,
	ALC256_FIXUP_CHROME_BOOK,
	ALC245_FIXUP_CLEVO_NOISY_MIC,
	ALC269_FIXUP_VAIO_VJFH52_MIC_NO_PRESENCE,
	ALC233_FIXUP_MEDION_MTL_SPK,
	ALC294_FIXUP_BASS_SPEAKER_15,
	ALC283_FIXUP_DELL_HP_RESUME,
	ALC294_FIXUP_ASUS_CS35L41_SPI_2,
	ALC274_FIXUP_HP_AIO_BIND_DACS,
	ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2,
	ALC285_FIXUP_ASUS_GA605K_HEADSET_MIC,
	ALC285_FIXUP_ASUS_GA605K_I2C_SPEAKER2_TO_DAC1,
	ALC269_FIXUP_POSITIVO_P15X_HEADSET_MIC,
	ALC289_FIXUP_ASUS_ZEPHYRUS_DUAL_SPK,
	ALC256_FIXUP_VAIO_RPL_MIC_NO_PRESENCE,
};

/* A special fixup for Lenovo C940 and Yoga Duet 7;
 * both have the very same PCI SSID, and we need to apply different fixups
 * depending on the codec ID
 */
static void alc298_fixup_lenovo_c940_duet7(struct hda_codec *codec,
					   const struct hda_fixup *fix,
					   int action)
{
	int id;

	if (codec->core.vendor_id == 0x10ec0298)
		id = ALC298_FIXUP_LENOVO_SPK_VOLUME; /* C940 */
	else
		id = ALC287_FIXUP_YOGA7_14ITL_SPEAKERS; /* Duet 7 */
	__snd_hda_apply_fixup(codec, id, action, 0);
}

static const struct hda_fixup alc269_fixups[] = {
	[ALC269_FIXUP_GPIO2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_gpio2,
	},
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
	[ALC269_FIXUP_ASUS_N7601ZM_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03A11050 },
			{ 0x1a, 0x03A11C30 },
			{ 0x21, 0x03211420 },
			{ }
		}
	},
	[ALC269_FIXUP_ASUS_N7601ZM] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x20, AC_VERB_SET_COEF_INDEX, 0x62},
			{0x20, AC_VERB_SET_PROC_COEF, 0xa007},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x10},
			{0x20, AC_VERB_SET_PROC_COEF, 0x8420},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x0f},
			{0x20, AC_VERB_SET_PROC_COEF, 0x7774},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_ASUS_N7601ZM_PINS,
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
		.v.func = alc_fixup_headset_mic,
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
	[ALC269VB_FIXUP_INFINIX_ZERO_BOOK_13] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x90170151 }, /* use as internal speaker (LFE) */
			{ 0x1b, 0x90170152 }, /* use as internal speaker (back) */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	},
	[ALC269VC_FIXUP_INFINIX_Y4_MAX] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x90170150 }, /* use as internal speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	},
	[ALC269VB_FIXUP_CHUWI_COREBOOK_XPRO] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x03a19020 }, /* headset mic */
			{ 0x1b, 0x90170150 }, /* speaker */
			{ }
		},
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
	[ALC269_FIXUP_DELL1_LIMIT_INT_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL1_MIC_NO_PRESENCE
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
	[ALC269VB_FIXUP_ASUS_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a110f0 },  /* use as headset mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
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
	[ALC269_FIXUP_LENOVO_XPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_ideapad_acpi,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
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
	[ALC255_FIXUP_DELL1_LIMIT_INT_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
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
		.v.func = alc_fixup_micmute_led,
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
	[ALC269VB_FIXUP_ASPIRE_E1_COEF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269vb_fixup_aspire_e1_coef,
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
	[ALC245_FIXUP_HP_X360_AMP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_x360_amp,
		.chained = true,
		.chain_id = ALC245_FIXUP_HP_GPIO_LED
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
		.v.func = alc_fixup_micmute_led,
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
	[ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc233_fixup_lenovo_low_en_micmute_led,
	},
	[ALC233_FIXUP_INTEL_NUC8_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
		.chained = true,
		.chain_id = ALC233_FIXUP_INTEL_NUC8_BOOST,
	},
	[ALC233_FIXUP_INTEL_NUC8_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost
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
	[ALC298_FIXUP_LENOVO_SPK_VOLUME] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_speaker_volume,
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
	[ALC285_FIXUP_ASUS_SPEAKER2_TO_DAC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC245_FIXUP_CS35L41_SPI_2
	},
	[ALC285_FIXUP_ASUS_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 },
			{ 0x1b, 0x03a11c30 },
			{ }
		},
		.chained = true,
		.chain_id = ALC285_FIXUP_ASUS_SPEAKER2_TO_DAC1
	},
	[ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x90170120 },
			{ }
		},
		.chained = true,
		.chain_id = ALC285_FIXUP_ASUS_HEADSET_MIC
	},
	[ALC285_FIXUP_ASUS_I2C_SPEAKER2_TO_DAC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC287_FIXUP_CS35L41_I2C_2
	},
	[ALC285_FIXUP_ASUS_I2C_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 },
			{ 0x1b, 0x03a11c30 },
			{ }
		},
		.chained = true,
		.chain_id = ALC285_FIXUP_ASUS_I2C_SPEAKER2_TO_DAC1
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
	[ALC221_FIXUP_HP_288PRO_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01813030 }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
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
		.chained = true,
		.chain_id = ALC269_FIXUP_GPIO2
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
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC294_FIXUP_ASUS_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1103c }, /* use as headset mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC294_FIXUP_ASUS_I2C_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a19020 }, /* use as headset mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC287_FIXUP_CS35L41_I2C_2
	},
	[ALC294_FIXUP_ASUS_SPK] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Set EAPD high */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x40 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x8800 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0f },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x7774 },
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
	[ALC255_FIXUP_PREDATOR_SUBWOOFER] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170151 }, /* use as internal speaker (LFE) */
			{ 0x1b, 0x90170152 } /* use as internal speaker (back) */
		}
	},
	[ALC299_FIXUP_PREDATOR_SPK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x21, 0x90170150 }, /* use as headset mic, without its own jack detect */
			{ }
		}
	},
	[ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_i2c_two,
		.chained = true,
		.chain_id = ALC255_FIXUP_PREDATOR_SUBWOOFER
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
	[ALC289_FIXUP_DELL_SPK1] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x90170140 },
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL4_MIC_NO_PRESENCE
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
	[ALC289_FIXUP_RTK_AMP_DUAL_SPK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC289_FIXUP_DELL_SPK1
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
	[ALC294_FIXUP_ASUS_ALLY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_i2c_two,
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_ALLY_PINS
	},
	[ALC294_FIXUP_ASUS_ALLY_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 },
			{ 0x1a, 0x03a11c30 },
			{ 0x21, 0x03211420 },
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_ALLY_VERBS
	},
	[ALC294_FIXUP_ASUS_ALLY_VERBS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x45 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x5089 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x46 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0004 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x47 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xa47a },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x49 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0049},
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x4a },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x201b },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x6b },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x4278},
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_ALLY_SPEAKER
	},
	[ALC294_FIXUP_ASUS_ALLY_SPEAKER] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
	},
	[ALC285_FIXUP_THINKPAD_X1_GEN7] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_thinkpad_x1_gen7,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI
	},
	[ALC285_FIXUP_THINKPAD_HEADSET_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_jack,
		.chained = true,
		.chain_id = ALC285_FIXUP_THINKPAD_X1_GEN7
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
	[ALC294_FIXUP_ASUS_GX502_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 }, /* front HP mic */
			{ 0x1a, 0x01a11830 }, /* rear external mic */
			{ 0x21, 0x03211020 }, /* front HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_GX502_VERBS
	},
	[ALC294_FIXUP_ASUS_GX502_VERBS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* set 0x15 to HP-OUT ctrl */
			{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
			/* unmute the 0x15 amp */
			{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000 },
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_GX502_HP
	},
	[ALC294_FIXUP_ASUS_GX502_HP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc294_fixup_gx502_hp,
	},
	[ALC295_FIXUP_DELL_TAS2781_I2C] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = tas2781_fixup_tias_i2c,
		.chained = true,
		.chain_id = ALC289_FIXUP_DUAL_SPK
	},
	[ALC294_FIXUP_ASUS_GU502_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a11050 }, /* rear HP mic */
			{ 0x1a, 0x01a11830 }, /* rear external mic */
			{ 0x21, 0x012110f0 }, /* rear HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_GU502_VERBS
	},
	[ALC294_FIXUP_ASUS_GU502_VERBS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* set 0x15 to HP-OUT ctrl */
			{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
			/* unmute the 0x15 amp */
			{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000 },
			/* set 0x1b to HP-OUT */
			{ 0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_GU502_HP
	},
	[ALC294_FIXUP_ASUS_GU502_HP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc294_fixup_gu502_hp,
	},
	 [ALC294_FIXUP_ASUS_G513_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
				{ 0x19, 0x03a11050 }, /* front HP mic */
				{ 0x1a, 0x03a11c30 }, /* rear external mic */
				{ 0x21, 0x03211420 }, /* front HP out */
				{ }
		},
	},
	[ALC285_FIXUP_ASUS_G533Z_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x90170152 }, /* Speaker Surround Playback Switch */
			{ 0x19, 0x03a19020 }, /* Mic Boost Volume */
			{ 0x1a, 0x03a11c30 }, /* Mic Boost Volume */
			{ 0x1e, 0x90170151 }, /* Rear jack, IN OUT EAPD Detect */
			{ 0x21, 0x03211420 },
			{ }
		},
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
		.chained = true,
		.chain_id = ALC289_FIXUP_ASUS_GA401,
	},
	[ALC285_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_gpio_led,
	},
	[ALC285_FIXUP_HP_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_mute_led,
	},
	[ALC285_FIXUP_HP_SPECTRE_X360_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_spectre_x360_mute_led,
	},
	[ALC285_FIXUP_HP_BEEP_MICMUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_beep,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_MUTE_LED,
	},
	[ALC236_FIXUP_HP_MUTE_LED_COEFBIT2] = {
	    .type = HDA_FIXUP_FUNC,
	    .v.func = alc236_fixup_hp_mute_led_coefbit2,
	},
	[ALC236_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc236_fixup_hp_gpio_led,
	},
	[ALC236_FIXUP_HP_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc236_fixup_hp_mute_led,
	},
	[ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc236_fixup_hp_mute_led_micmute_vref,
	},
	[ALC236_FIXUP_LENOVO_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
		.chained = true,
		.chain_id = ALC283_FIXUP_INT_MIC,
	},
	[ALC295_FIXUP_HP_MUTE_LED_COEFBIT11] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc295_fixup_hp_mute_led_coefbit11,
	},
	[ALC298_FIXUP_SAMSUNG_AMP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_samsung_amp,
		.chained = true,
		.chain_id = ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET
	},
	[ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_samsung_amp_v2_2_amps
	},
	[ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_samsung_amp_v2_4_amps
	},
	[ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc5 },
			{ }
		},
	},
	[ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x08},
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2fcf},
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
	[ALC269VC_FIXUP_ACER_VCOPPERBOX_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x90100120 }, /* use as internal speaker */
			{ 0x18, 0x02a111f0 }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01011020 }, /* use as line out */
			{ },
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC269VC_FIXUP_ACER_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x02a11030 }, /* use as headset mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC269VC_FIXUP_ACER_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a11130 }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC289_FIXUP_ASUS_GA401] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc289_fixup_asus_ga401,
		.chained = true,
		.chain_id = ALC289_FIXUP_ASUS_GA502,
	},
	[ALC289_FIXUP_ASUS_GA502] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11020 }, /* headset mic with jack detect */
			{ }
		},
	},
	[ALC256_FIXUP_ACER_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x02a11120 }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC256_FIXUP_ASUS_HEADSET_MODE
	},
	[ALC285_FIXUP_HP_GPIO_AMP_INIT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_gpio_amp_init,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_GPIO_LED
	},
	[ALC269_FIXUP_CZC_B20] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x411111f0 },
			{ 0x14, 0x90170110 }, /* speaker */
			{ 0x15, 0x032f1020 }, /* HP out */
			{ 0x17, 0x411111f0 },
			{ 0x18, 0x03ab1040 }, /* mic */
			{ 0x19, 0xb7a7013f },
			{ 0x1a, 0x0181305f },
			{ 0x1b, 0x411111f0 },
			{ 0x1d, 0x411111f0 },
			{ 0x1e, 0x411111f0 },
			{ }
		},
		.chain_id = ALC269_FIXUP_DMIC,
	},
	[ALC269_FIXUP_CZC_TMI] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x4000c000 },
			{ 0x14, 0x90170110 }, /* speaker */
			{ 0x15, 0x0421401f }, /* HP out */
			{ 0x17, 0x411111f0 },
			{ 0x18, 0x04a19020 }, /* mic */
			{ 0x19, 0x411111f0 },
			{ 0x1a, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1d, 0x40448505 },
			{ 0x1e, 0x411111f0 },
			{ 0x20, 0x8000ffff },
			{ }
		},
		.chain_id = ALC269_FIXUP_DMIC,
	},
	[ALC269_FIXUP_CZC_L101] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x40000000 },
			{ 0x14, 0x01014010 }, /* speaker */
			{ 0x15, 0x411111f0 }, /* HP out */
			{ 0x16, 0x411111f0 },
			{ 0x18, 0x01a19020 }, /* mic */
			{ 0x19, 0x02a19021 },
			{ 0x1a, 0x0181302f },
			{ 0x1b, 0x0221401f },
			{ 0x1c, 0x411111f0 },
			{ 0x1d, 0x4044c601 },
			{ 0x1e, 0x411111f0 },
			{ }
		},
		.chain_id = ALC269_FIXUP_DMIC,
	},
	[ALC269_FIXUP_LEMOTE_A1802] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x40000000 },
			{ 0x14, 0x90170110 }, /* speaker */
			{ 0x17, 0x411111f0 },
			{ 0x18, 0x03a19040 }, /* mic1 */
			{ 0x19, 0x90a70130 }, /* mic2 */
			{ 0x1a, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1d, 0x40489d2d },
			{ 0x1e, 0x411111f0 },
			{ 0x20, 0x0003ffff },
			{ 0x21, 0x03214020 },
			{ }
		},
		.chain_id = ALC269_FIXUP_DMIC,
	},
	[ALC269_FIXUP_LEMOTE_A190X] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121401f }, /* HP out */
			{ 0x18, 0x01a19c20 }, /* rear  mic */
			{ 0x19, 0x99a3092f }, /* front mic */
			{ 0x1b, 0x0201401f }, /* front lineout */
			{ }
		},
		.chain_id = ALC269_FIXUP_DMIC,
	},
	[ALC256_FIXUP_INTEL_NUC8_RUGGED] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC256_FIXUP_INTEL_NUC10] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC255_FIXUP_XIAOMI_HEADSET_MIC] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x45 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x5089 },
			{ }
		},
		.chained = true,
		.chain_id = ALC289_FIXUP_ASUS_GA502
	},
	[ALC274_FIXUP_HP_MIC] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x45 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x5089 },
			{ }
		},
	},
	[ALC274_FIXUP_HP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc274_fixup_hp_headset_mic,
		.chained = true,
		.chain_id = ALC274_FIXUP_HP_MIC
	},
	[ALC274_FIXUP_HP_ENVY_GPIO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc274_fixup_hp_envy_gpio,
	},
	[ALC274_FIXUP_ASUS_ZEN_AIO_27] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x10 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xc420 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x40 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x8800 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x49 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0249 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x4a },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x202b },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x62 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xa007 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x6b },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x5060 },
			{}
		},
		.chained = true,
		.chain_id = ALC2XX_FIXUP_HEADSET_MIC,
	},
	[ALC256_FIXUP_ASUS_HPE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Set EAPD high */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0f },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x7778 },
			{ }
		},
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_HEADSET_MIC
	},
	[ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_jack,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI
	},
	[ALC287_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_hp_gpio_led,
	},
	[ALC256_FIXUP_HP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc274_fixup_hp_headset_mic,
	},
	[ALC236_FIXUP_DELL_AIO_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_int_mic,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC282_FIXUP_ACER_DISABLE_LINEOUT] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x411111f0 },
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ },
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC255_FIXUP_ACER_LIMIT_INT_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC255_FIXUP_ACER_MIC_NO_PRESENCE,
	},
	[ALC256_FIXUP_ACER_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x02a1113c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x90a1092f }, /* use as internal mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC285_FIXUP_IDEAPAD_S740_COEF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_ideapad_s740_coef,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC285_FIXUP_HP_LIMIT_INT_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_MUTE_LED,
	},
	[ALC295_FIXUP_ASUS_DACS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc295_fixup_asus_dacs,
	},
	[ALC295_FIXUP_HP_OMEN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0xb7a60130 },
			{ 0x13, 0x40000000 },
			{ 0x14, 0x411111f0 },
			{ 0x16, 0x411111f0 },
			{ 0x17, 0x90170110 },
			{ 0x18, 0x411111f0 },
			{ 0x19, 0x02a11030 },
			{ 0x1a, 0x411111f0 },
			{ 0x1b, 0x04a19030 },
			{ 0x1d, 0x40600001 },
			{ 0x1e, 0x411111f0 },
			{ 0x21, 0x03211020 },
			{}
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HP_LINE1_MIC1_LED,
	},
	[ALC285_FIXUP_HP_SPECTRE_X360] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_spectre_x360,
	},
	[ALC285_FIXUP_HP_SPECTRE_X360_EB1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_spectre_x360_eb1
	},
	[ALC285_FIXUP_HP_SPECTRE_X360_DF1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_spectre_x360_df1
	},
	[ALC285_FIXUP_HP_ENVY_X360] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_hp_envy_x360,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_GPIO_AMP_INIT,
	},
	[ALC287_FIXUP_IDEAPAD_BASS_SPK_AMP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_ideapad_s740_coef,
		.chained = true,
		.chain_id = ALC285_FIXUP_THINKPAD_HEADSET_JACK,
	},
	[ALC623_FIXUP_LENOVO_THINKSTATION_P340] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_shutup,
		.chained = true,
		.chain_id = ALC283_FIXUP_HEADSET_MIC,
	},
	[ALC255_FIXUP_ACER_HEADPHONE_AND_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x21, 0x03211030 }, /* Change the Headphone location to Left */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_XIAOMI_HEADSET_MIC
	},
	[ALC236_FIXUP_HP_LIMIT_INT_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF,
	},
	[ALC285_FIXUP_LEGION_Y9000X_SPEAKERS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_ideapad_s740_coef,
		.chained = true,
		.chain_id = ALC285_FIXUP_LEGION_Y9000X_AUTOMUTE,
	},
	[ALC285_FIXUP_LEGION_Y9000X_AUTOMUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_legion_15imhg05_speakers,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS] = {
		.type = HDA_FIXUP_VERBS,
		//.v.verbs = legion_15imhg05_coefs,
		.v.verbs = (const struct hda_verb[]) {
			 // set left speaker Legion 7i.
			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x41 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xc },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x1a },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			 // set right speaker Legion 7i.
			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x42 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xc },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x2a },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },
			 {}
		},
		.chained = true,
		.chain_id = ALC287_FIXUP_LEGION_15IMHG05_AUTOMUTE,
	},
	[ALC287_FIXUP_LEGION_15IMHG05_AUTOMUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_legion_15imhg05_speakers,
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE,
	},
	[ALC287_FIXUP_YOGA7_14ITL_SPEAKERS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			 // set left speaker Yoga 7i.
			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x41 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xc },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x1a },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			 // set right speaker Yoga 7i.
			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x46 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xc },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x2a },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },
			 {}
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE,
	},
	[ALC298_FIXUP_LENOVO_C940_DUET7] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_lenovo_c940_duet7,
	},
	[ALC287_FIXUP_13S_GEN2_SPEAKERS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x41 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x42 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },
			{}
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE,
	},
	[ALC256_FIXUP_SET_COEF_DEFAULTS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc256_fixup_set_coef_defaults,
	},
	[ALC245_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_gpio_led,
	},
	[ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11120 }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC,
	},
	[ALC233_FIXUP_NO_AUDIO_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc233_fixup_no_audio_jack,
	},
	[ALC256_FIXUP_MIC_NO_PRESENCE_AND_RESUME] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc256_fixup_mic_no_presence_and_resume,
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC287_FIXUP_LEGION_16ACHG6] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_legion_16achg6_speakers,
	},
	[ALC287_FIXUP_CS35L41_I2C_2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_i2c_two,
	},
	[ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_i2c_two,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_MUTE_LED,
	},
	[ALC287_FIXUP_CS35L41_I2C_4] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_i2c_four,
	},
	[ALC245_FIXUP_CS35L41_SPI_2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_spi_two,
	},
	[ALC245_FIXUP_CS35L41_SPI_1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_spi_one,
	},
	[ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_spi_two,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_GPIO_LED,
	},
	[ALC245_FIXUP_CS35L41_SPI_4] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_spi_four,
	},
	[ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_spi_four,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_GPIO_LED,
	},
	[ALC285_FIXUP_HP_SPEAKERS_MICMUTE_LED] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			 { 0x20, AC_VERB_SET_COEF_INDEX, 0x19 },
			 { 0x20, AC_VERB_SET_PROC_COEF, 0x8e11 },
			 { }
		},
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_MUTE_LED,
	},
	[ALC269_FIXUP_DELL4_MIC_NO_PRESENCE_QUIET] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell4_mic_no_presence_quiet,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
	},
	[ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x02a1112c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC287_FIXUP_LEGION_16ITHG6] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_legion_16ithg6_speakers,
	},
	[ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			// enable left speaker
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x41 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xc },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x1a },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xf },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x42 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x10 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x40 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			// enable right speaker
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x24 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x46 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xc },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2a },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xf },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x46 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x10 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x44 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x26 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0xb020 },

			{ },
		},
	},
	[ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_yoga9_14iap7_bass_spk_pin,
		.chained = true,
		.chain_id = ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK,
	},
	[ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_yoga9_14iap7_bass_spk_pin,
		.chained = true,
		.chain_id = ALC287_FIXUP_CS35L41_I2C_2,
	},
	[ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc295_fixup_dell_inspiron_top_speakers,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
	},
	[ALC236_FIXUP_DELL_DUAL_CODECS] = {
		.type = HDA_FIXUP_PINS,
		.v.func = alc1220_fixup_gb_dual_codecs,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
	},
	[ALC287_FIXUP_CS35L41_I2C_2_THINKPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_i2c_two,
		.chained = true,
		.chain_id = ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK,
	},
	[ALC287_FIXUP_TAS2781_I2C] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = tas2781_fixup_tias_i2c,
		.chained = true,
		.chain_id = ALC285_FIXUP_THINKPAD_HEADSET_JACK,
	},
	[ALC245_FIXUP_TAS2781_SPI_2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = tas2781_fixup_spi,
		.chained = true,
		.chain_id = ALC285_FIXUP_HP_GPIO_LED,
	},
	[ALC287_FIXUP_TXNW2781_I2C] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = tas2781_fixup_txnw_i2c,
		.chained = true,
		.chain_id = ALC285_FIXUP_THINKPAD_HEADSET_JACK,
	},
	[ALC287_FIXUP_YOGA7_14ARB7_I2C] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = yoga7_14arb7_fixup_i2c,
		.chained = true,
		.chain_id = ALC285_FIXUP_THINKPAD_HEADSET_JACK,
	},
	[ALC245_FIXUP_HP_MUTE_LED_COEFBIT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_mute_led_coefbit,
	},
	[ALC245_FIXUP_HP_MUTE_LED_V1_COEFBIT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_mute_led_v1_coefbit,
	},
	[ALC245_FIXUP_HP_X360_MUTE_LEDS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_mute_led_coefbit,
		.chained = true,
		.chain_id = ALC245_FIXUP_HP_GPIO_LED
	},
	[ALC287_FIXUP_THINKPAD_I2S_SPK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_bind_dacs,
		.chained = true,
		.chain_id = ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK,
	},
	[ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_bind_dacs,
		.chained = true,
		.chain_id = ALC287_FIXUP_CS35L41_I2C_2_THINKPAD_ACPI,
	},
	[ALC2XX_FIXUP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc2xx_fixup_headset_mic,
	},
	[ALC289_FIXUP_DELL_CS35L41_SPI_2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_spi_two,
		.chained = true,
		.chain_id = ALC289_FIXUP_DUAL_SPK
	},
	[ALC294_FIXUP_CS35L41_I2C_2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_i2c_two,
	},
	[ALC256_FIXUP_ACER_SFG16_MICMUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc256_fixup_acer_sfg16_micmute_led,
	},
	[ALC256_FIXUP_HEADPHONE_AMP_VOL] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc256_decrease_headphone_amp_val,
	},
	[ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_spectre_x360_eu0xxx,
	},
	[ALC245_FIXUP_HP_SPECTRE_X360_16_AA0XXX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_spectre_x360_16_aa0xxx,
	},
	[ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc245_fixup_hp_zbook_firefly_g12a,
	},
	[ALC285_FIXUP_ASUS_GA403U] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_asus_ga403u,
	},
	[ALC285_FIXUP_ASUS_GA403U_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 },
			{ 0x1b, 0x03a11c30 },
			{ }
		},
		.chained = true,
		.chain_id = ALC285_FIXUP_ASUS_GA403U_I2C_SPEAKER2_TO_DAC1
	},
	[ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC285_FIXUP_ASUS_GU605_SPI_2_HEADSET_MIC,
	},
	[ALC285_FIXUP_ASUS_GU605_SPI_2_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 },
			{ 0x1b, 0x03a11c30 },
			{ }
		},
	},
	[ALC285_FIXUP_ASUS_GA403U_I2C_SPEAKER2_TO_DAC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
		.chained = true,
		.chain_id = ALC285_FIXUP_ASUS_GA403U,
	},
	[ALC287_FIXUP_LENOVO_THKPAD_WH_ALC1318] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc287_fixup_lenovo_thinkpad_with_alc1318,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI
	},
	[ALC256_FIXUP_CHROME_BOOK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc256_fixup_chromebook,
		.chained = true,
		.chain_id = ALC225_FIXUP_HEADSET_JACK
	},
	[ALC245_FIXUP_CLEVO_NOISY_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE,
	},
	[ALC269_FIXUP_VAIO_VJFH52_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			{ 0x1b, 0x20a11040 }, /* dock mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	},
	[ALC233_FIXUP_MEDION_MTL_SPK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x90170110 },
			{ }
		},
	},
	[ALC294_FIXUP_BASS_SPEAKER_15] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc294_fixup_bass_speaker_15,
	},
	[ALC283_FIXUP_DELL_HP_RESUME] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc283_fixup_dell_hp_resume,
	},
	[ALC294_FIXUP_ASUS_CS35L41_SPI_2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs35l41_fixup_spi_two,
		.chained = true,
		.chain_id = ALC294_FIXUP_ASUS_HEADSET_MIC,
	},
	[ALC274_FIXUP_HP_AIO_BIND_DACS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc274_fixup_hp_aio_bind_dacs,
	},
	[ALC285_FIXUP_ASUS_GA605K_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a11050 },
			{ 0x1b, 0x03a11c30 },
			{ }
		},
		.chained = true,
		.chain_id = ALC285_FIXUP_ASUS_GA605K_I2C_SPEAKER2_TO_DAC1
	},
	[ALC285_FIXUP_ASUS_GA605K_I2C_SPEAKER2_TO_DAC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc285_fixup_speaker2_to_dac1,
	},
	[ALC269_FIXUP_POSITIVO_P15X_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269VC_FIXUP_ACER_MIC_NO_PRESENCE,
	},
	[ALC289_FIXUP_ASUS_ZEPHYRUS_DUAL_SPK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170151 }, /* Internal Speaker LFE */
			{ 0x1e, 0x90170150 }, /* Internal Speaker */
			{ }
		},
	},
	[ALC256_FIXUP_VAIO_RPL_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x22a190a0 }, /* dock mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	}
};

static const struct hda_quirk alc269_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0283, "Acer TravelMate 8371", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x029b, "Acer 1810TZ", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0349, "Acer AOD260", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x047c, "Acer AC700", ALC269_FIXUP_ACER_AC700),
	SND_PCI_QUIRK(0x1025, 0x072d, "Acer Aspire V5-571G", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x0740, "Acer AO725", ALC271_FIXUP_HP_GATE_MIC_JACK),
	SND_PCI_QUIRK(0x1025, 0x0742, "Acer AO756", ALC271_FIXUP_HP_GATE_MIC_JACK),
	SND_PCI_QUIRK(0x1025, 0x0762, "Acer Aspire E1-472", ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572),
	SND_PCI_QUIRK(0x1025, 0x0775, "Acer Aspire E1-572", ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572),
	SND_PCI_QUIRK(0x1025, 0x079b, "Acer Aspire V5-573G", ALC282_FIXUP_ASPIRE_V5_PINS),
	SND_PCI_QUIRK(0x1025, 0x080d, "Acer Aspire V5-122P", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x0840, "Acer Aspire E1", ALC269VB_FIXUP_ASPIRE_E1_COEF),
	SND_PCI_QUIRK(0x1025, 0x100c, "Acer Aspire E5-574G", ALC255_FIXUP_ACER_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1025, 0x101c, "Acer Veriton N2510G", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x1025, 0x102b, "Acer Aspire C24-860", ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1065, "Acer Aspire C20-820", ALC269VC_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x106d, "Acer Cloudbook 14", ALC283_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x1025, 0x1094, "Acer Aspire E5-575T", ALC255_FIXUP_ACER_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1025, 0x1099, "Acer Aspire E5-523G", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x110e, "Acer Aspire ES1-432", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1166, "Acer Veriton N4640G", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x1025, 0x1167, "Acer Veriton N6640G", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x1025, 0x1177, "Acer Predator G9-593", ALC255_FIXUP_PREDATOR_SUBWOOFER),
	SND_PCI_QUIRK(0x1025, 0x1178, "Acer Predator G9-593", ALC255_FIXUP_PREDATOR_SUBWOOFER),
	SND_PCI_QUIRK(0x1025, 0x1246, "Acer Predator Helios 500", ALC299_FIXUP_PREDATOR_SPK),
	SND_PCI_QUIRK(0x1025, 0x1247, "Acer vCopperbox", ALC269VC_FIXUP_ACER_VCOPPERBOX_PINS),
	SND_PCI_QUIRK(0x1025, 0x1248, "Acer Veriton N4660G", ALC269VC_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1269, "Acer SWIFT SF314-54", ALC256_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x126a, "Acer Swift SF114-32", ALC256_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x128f, "Acer Veriton Z6860G", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1290, "Acer Veriton Z4860G", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1291, "Acer Veriton Z4660G", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x129c, "Acer SWIFT SF314-55", ALC256_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x129d, "Acer SWIFT SF313-51", ALC256_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1300, "Acer SWIFT SF314-56", ALC256_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1308, "Acer Aspire Z24-890", ALC286_FIXUP_ACER_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x132a, "Acer TravelMate B114-21", ALC233_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1330, "Acer TravelMate X514-51T", ALC255_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x1360, "Acer Aspire A115", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x141f, "Acer Spin SP513-54N", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x142b, "Acer Swift SF314-42", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1430, "Acer TravelMate B311R-31", ALC256_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x1466, "Acer Aspire A515-56", ALC255_FIXUP_ACER_HEADPHONE_AND_MIC),
	SND_PCI_QUIRK(0x1025, 0x1534, "Acer Predator PH315-54", ALC255_FIXUP_ACER_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1025, 0x159c, "Acer Nitro 5 AN515-58", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x169a, "Acer Swift SFG16", ALC256_FIXUP_ACER_SFG16_MICMUTE_LED),
	SND_PCI_QUIRK(0x1025, 0x1826, "Acer Helios ZPC", ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1025, 0x182c, "Acer Helios ZPD", ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1025, 0x1844, "Acer Helios ZPS", ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1028, 0x0470, "Dell M101z", ALC269_FIXUP_DELL_M101Z),
	SND_PCI_QUIRK(0x1028, 0x053c, "Dell Latitude E5430", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x054b, "Dell XPS one 2710", ALC275_FIXUP_DELL_XPS),
	SND_PCI_QUIRK(0x1028, 0x05bd, "Dell Latitude E6440", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05be, "Dell Latitude E6540", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05ca, "Dell Latitude E7240", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05cb, "Dell Latitude E7440", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05da, "Dell Vostro 5460", ALC290_FIXUP_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x05f4, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05f5, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05f6, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0604, "Dell Venue 11 Pro 7130", ALC283_FIXUP_DELL_HP_RESUME),
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
	SND_PCI_QUIRK(0x1028, 0x0798, "Dell Inspiron 17 7000 Gaming", ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x07b0, "Dell Precision 7520", ALC295_FIXUP_DISABLE_DAC3),
	SND_PCI_QUIRK(0x1028, 0x080c, "Dell WYSE", ALC225_FIXUP_DELL_WYSE_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x084b, "Dell", ALC274_FIXUP_DELL_AIO_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x084e, "Dell", ALC274_FIXUP_DELL_AIO_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x0871, "Dell Precision 3630", ALC255_FIXUP_DELL_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0872, "Dell Precision 3630", ALC255_FIXUP_DELL_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0873, "Dell Precision 3930", ALC255_FIXUP_DUMMY_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x0879, "Dell Latitude 5420 Rugged", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x08ad, "Dell WYSE AIO", ALC225_FIXUP_DELL_WYSE_AIO_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x08ae, "Dell WYSE NB", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0935, "Dell", ALC274_FIXUP_DELL_AIO_LINEOUT_VERB),
	SND_PCI_QUIRK(0x1028, 0x097d, "Dell Precision", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x097e, "Dell Precision", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x098d, "Dell Precision", ALC233_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x09bf, "Dell Precision", ALC233_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0a2e, "Dell", ALC236_FIXUP_DELL_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0a30, "Dell", ALC236_FIXUP_DELL_AIO_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0a38, "Dell Latitude 7520", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE_QUIET),
	SND_PCI_QUIRK(0x1028, 0x0a58, "Dell", ALC255_FIXUP_DELL_HEADSET_MIC),
	SND_PCI_QUIRK(0x1028, 0x0a61, "Dell XPS 15 9510", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0a62, "Dell Precision 5560", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0a9d, "Dell Latitude 5430", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0a9e, "Dell Latitude 5430", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0b19, "Dell XPS 15 9520", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0b1a, "Dell Precision 5570", ALC289_FIXUP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0b27, "Dell", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0b28, "Dell", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0b37, "Dell Inspiron 16 Plus 7620 2-in-1", ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS),
	SND_PCI_QUIRK(0x1028, 0x0b71, "Dell Inspiron 16 Plus 7620", ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS),
	SND_PCI_QUIRK(0x1028, 0x0beb, "Dell XPS 15 9530 (2023)", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0c03, "Dell Precision 5340", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0c0b, "Dell Oasis 14 RPL-P", ALC289_FIXUP_RTK_AMP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0c0d, "Dell Oasis", ALC289_FIXUP_RTK_AMP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0c0e, "Dell Oasis 16", ALC289_FIXUP_RTK_AMP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0c19, "Dell Precision 3340", ALC236_FIXUP_DELL_DUAL_CODECS),
	SND_PCI_QUIRK(0x1028, 0x0c1a, "Dell Precision 3340", ALC236_FIXUP_DELL_DUAL_CODECS),
	SND_PCI_QUIRK(0x1028, 0x0c1b, "Dell Precision 3440", ALC236_FIXUP_DELL_DUAL_CODECS),
	SND_PCI_QUIRK(0x1028, 0x0c1c, "Dell Precision 3540", ALC236_FIXUP_DELL_DUAL_CODECS),
	SND_PCI_QUIRK(0x1028, 0x0c1d, "Dell Precision 3440", ALC236_FIXUP_DELL_DUAL_CODECS),
	SND_PCI_QUIRK(0x1028, 0x0c1e, "Dell Precision 3540", ALC236_FIXUP_DELL_DUAL_CODECS),
	SND_PCI_QUIRK(0x1028, 0x0c28, "Dell Inspiron 16 Plus 7630", ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS),
	SND_PCI_QUIRK(0x1028, 0x0c4d, "Dell", ALC287_FIXUP_CS35L41_I2C_4),
	SND_PCI_QUIRK(0x1028, 0x0c94, "Dell Polaris 3 metal", ALC295_FIXUP_DELL_TAS2781_I2C),
	SND_PCI_QUIRK(0x1028, 0x0c96, "Dell Polaris 2in1", ALC295_FIXUP_DELL_TAS2781_I2C),
	SND_PCI_QUIRK(0x1028, 0x0cbd, "Dell Oasis 13 CS MTL-U", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0cbe, "Dell Oasis 13 2-IN-1 MTL-U", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0cbf, "Dell Oasis 13 Low Weight MTU-L", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0cc0, "Dell Oasis 13", ALC289_FIXUP_RTK_AMP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x0cc1, "Dell Oasis 14 MTL-H/U", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0cc2, "Dell Oasis 14 2-in-1 MTL-H/U", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0cc3, "Dell Oasis 14 Low Weight MTL-U", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0cc4, "Dell Oasis 16 MTL-H/U", ALC289_FIXUP_DELL_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1028, 0x0cc5, "Dell Oasis 14", ALC289_FIXUP_RTK_AMP_DUAL_SPK),
	SND_PCI_QUIRK(0x1028, 0x164a, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x164b, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x1586, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC2),
	SND_PCI_QUIRK(0x103c, 0x18e6, "HP", ALC269_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x218b, "HP", ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x21f9, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2210, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2214, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x221b, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x221c, "HP EliteBook 755 G2", ALC280_FIXUP_HP_HEADSET_MIC),
	SND_PCI_QUIRK(0x103c, 0x2221, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2225, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2236, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2237, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2238, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2239, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x224b, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2253, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2254, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2255, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2256, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2257, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2259, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x225a, "HP", ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x225f, "HP", ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY),
	SND_PCI_QUIRK(0x103c, 0x2260, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2263, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2264, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2265, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2268, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226a, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226b, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2271, "HP", ALC286_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x2272, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2272, "HP", ALC280_FIXUP_HP_DOCK_PINS),
	SND_PCI_QUIRK(0x103c, 0x2273, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2273, "HP", ALC280_FIXUP_HP_DOCK_PINS),
	SND_PCI_QUIRK(0x103c, 0x2278, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x227f, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2282, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x228b, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x228e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x229e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22b2, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22b7, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22bf, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c4, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c5, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c7, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c8, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22cf, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22db, "HP", ALC280_FIXUP_HP_9480M),
	SND_PCI_QUIRK(0x103c, 0x22dc, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x22fb, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2334, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2335, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2336, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2337, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2b5e, "HP 288 Pro G2 MT", ALC221_FIXUP_HP_288PRO_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x802e, "HP Z240 SFF", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x802f, "HP Z240", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x8077, "HP", ALC256_FIXUP_HP_HEADSET_MIC),
	SND_PCI_QUIRK(0x103c, 0x8158, "HP", ALC256_FIXUP_HP_HEADSET_MIC),
	SND_PCI_QUIRK(0x103c, 0x820d, "HP Pavilion 15", ALC295_FIXUP_HP_X360),
	SND_PCI_QUIRK(0x103c, 0x8256, "HP", ALC221_FIXUP_HP_FRONT_MIC),
	SND_PCI_QUIRK(0x103c, 0x827e, "HP x360", ALC295_FIXUP_HP_X360),
	SND_PCI_QUIRK(0x103c, 0x827f, "HP x360", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x82bf, "HP G3 mini", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x82c0, "HP G3 mini premium", ALC221_FIXUP_HP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x83b9, "HP Spectre x360", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x841c, "HP Pavilion 15-CK0xx", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x8497, "HP Envy x360", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x84a6, "HP 250 G7 Notebook PC", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x84ae, "HP 15-db0403ng", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x84da, "HP OMEN dc0019-ur", ALC295_FIXUP_HP_OMEN),
	SND_PCI_QUIRK(0x103c, 0x84e7, "HP Pavilion 15", ALC269_FIXUP_HP_MUTE_LED_MIC3),
	SND_PCI_QUIRK(0x103c, 0x8519, "HP Spectre x360 15-df0xxx", ALC285_FIXUP_HP_SPECTRE_X360),
	SND_PCI_QUIRK(0x103c, 0x8537, "HP ProBook 440 G6", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8548, "HP EliteBook x360 830 G6", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x854a, "HP EliteBook 830 G6", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x85c6, "HP Pavilion x360 Convertible 14-dy1xxx", ALC295_FIXUP_HP_MUTE_LED_COEFBIT11),
	SND_PCI_QUIRK(0x103c, 0x85de, "HP Envy x360 13-ar0xxx", ALC285_FIXUP_HP_ENVY_X360),
	SND_PCI_QUIRK(0x103c, 0x8603, "HP Omen 17-cb0xxx", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x860c, "HP ZBook 17 G6", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x860f, "HP ZBook 15 G6", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x861f, "HP Elite Dragonfly G1", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x869d, "HP", ALC236_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x86c1, "HP Laptop 15-da3001TU", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x86c7, "HP Envy AiO 32", ALC274_FIXUP_HP_ENVY_GPIO),
	SND_PCI_QUIRK(0x103c, 0x86e7, "HP Spectre x360 15-eb0xxx", ALC285_FIXUP_HP_SPECTRE_X360_EB1),
	SND_PCI_QUIRK(0x103c, 0x863e, "HP Spectre x360 15-df1xxx", ALC285_FIXUP_HP_SPECTRE_X360_DF1),
	SND_PCI_QUIRK(0x103c, 0x86e8, "HP Spectre x360 15-eb0xxx", ALC285_FIXUP_HP_SPECTRE_X360_EB1),
	SND_PCI_QUIRK(0x103c, 0x86f9, "HP Spectre x360 13-aw0xxx", ALC285_FIXUP_HP_SPECTRE_X360_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x8716, "HP Elite Dragonfly G2 Notebook PC", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x8720, "HP EliteBook x360 1040 G8 Notebook PC", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x8724, "HP EliteBook 850 G7", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8728, "HP EliteBook 840 G7", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8729, "HP", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8730, "HP ProBook 445 G7", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8735, "HP ProBook 435 G7", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8736, "HP", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x8760, "HP EliteBook 8{4,5}5 G7", ALC285_FIXUP_HP_BEEP_MICMUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x876e, "HP ENVY x360 Convertible 13-ay0xxx", ALC245_FIXUP_HP_X360_MUTE_LEDS),
	SND_PCI_QUIRK(0x103c, 0x877a, "HP", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x877d, "HP", ALC236_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x8780, "HP ZBook Fury 17 G7 Mobile Workstation",
		      ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x8783, "HP ZBook Fury 15 G7 Mobile Workstation",
		      ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x8786, "HP OMEN 15", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x8787, "HP OMEN 15", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x8788, "HP OMEN 15", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x87b7, "HP Laptop 14-fq0xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x87c8, "HP", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87cc, "HP Pavilion 15-eg0xxx", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87d3, "HP Laptop 15-gw0xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x87df, "HP ProBook 430 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87e5, "HP ProBook 440 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87e7, "HP ProBook 450 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87f1, "HP ProBook 630 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87f2, "HP ProBook 640 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87f4, "HP", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87f5, "HP", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x87f6, "HP Spectre x360 14", ALC245_FIXUP_HP_X360_AMP),
	SND_PCI_QUIRK(0x103c, 0x87f7, "HP Spectre x360 14", ALC245_FIXUP_HP_X360_AMP),
	SND_PCI_QUIRK(0x103c, 0x87fd, "HP Laptop 14-dq2xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x87fe, "HP Laptop 15s-fq2xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x8805, "HP ProBook 650 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x880d, "HP EliteBook 830 G8 Notebook PC", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8811, "HP Spectre x360 15-eb1xxx", ALC285_FIXUP_HP_SPECTRE_X360_EB1),
	SND_PCI_QUIRK(0x103c, 0x8812, "HP Spectre x360 15-eb1xxx", ALC285_FIXUP_HP_SPECTRE_X360_EB1),
	SND_PCI_QUIRK(0x103c, 0x881d, "HP 250 G8 Notebook PC", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x881e, "HP Laptop 15s-du3xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x8846, "HP EliteBook 850 G8 Notebook PC", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8847, "HP EliteBook x360 830 G8 Notebook PC", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x884b, "HP EliteBook 840 Aero G8 Notebook PC", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x884c, "HP EliteBook 840 G8 Notebook PC", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8862, "HP ProBook 445 G8 Notebook PC", ALC236_FIXUP_HP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x103c, 0x8863, "HP ProBook 445 G8 Notebook PC", ALC236_FIXUP_HP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x103c, 0x886d, "HP ZBook Fury 17.3 Inch G8 Mobile Workstation PC", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x8870, "HP ZBook Fury 15.6 Inch G8 Mobile Workstation PC", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x8873, "HP ZBook Studio 15.6 Inch G8 Mobile Workstation PC", ALC285_FIXUP_HP_GPIO_AMP_INIT),
	SND_PCI_QUIRK(0x103c, 0x887a, "HP Laptop 15s-eq2xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x887c, "HP Laptop 14s-fq1xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x888a, "HP ENVY x360 Convertible 15-eu0xxx", ALC245_FIXUP_HP_X360_MUTE_LEDS),
	SND_PCI_QUIRK(0x103c, 0x888d, "HP ZBook Power 15.6 inch G8 Mobile Workstation PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8895, "HP EliteBook 855 G8 Notebook PC", ALC285_FIXUP_HP_SPEAKERS_MICMUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x8896, "HP EliteBook 855 G8 Notebook PC", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x8898, "HP EliteBook 845 G8 Notebook PC", ALC285_FIXUP_HP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x103c, 0x88d0, "HP Pavilion 15-eh1xxx (mainboard 88D0)", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x88dd, "HP Pavilion 15z-ec200", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x8902, "HP OMEN 16", ALC285_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x890e, "HP 255 G8 Notebook PC", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x8919, "HP Pavilion Aero Laptop 13-be0xxx", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x896d, "HP ZBook Firefly 16 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x896e, "HP EliteBook x360 830 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8971, "HP EliteBook 830 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8972, "HP EliteBook 840 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8973, "HP EliteBook 860 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8974, "HP EliteBook 840 Aero G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8975, "HP EliteBook x360 840 Aero G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x897d, "HP mt440 Mobile Thin Client U74", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8981, "HP Elite Dragonfly G3", ALC245_FIXUP_CS35L41_SPI_4),
	SND_PCI_QUIRK(0x103c, 0x898a, "HP Pavilion 15-eg100", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x898e, "HP EliteBook 835 G9", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x898f, "HP EliteBook 835 G9", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8991, "HP EliteBook 845 G9", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8992, "HP EliteBook 845 G9", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8994, "HP EliteBook 855 G9", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8995, "HP EliteBook 855 G9", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x89a0, "HP Laptop 15-dw4xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x89a4, "HP ProBook 440 G9", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89a6, "HP ProBook 450 G9", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89aa, "HP EliteBook 630 G9", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89ac, "HP EliteBook 640 G9", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89ae, "HP EliteBook 650 G9", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89c0, "HP ZBook Power 15.6 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89c3, "Zbook Studio G9", ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89c6, "Zbook Fury 17 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x89ca, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x89d3, "HP EliteBook 645 G9 (MB 89D2)", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x89da, "HP Spectre x360 14t-ea100", ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX),
	SND_PCI_QUIRK(0x103c, 0x89e7, "HP Elite x2 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8a0f, "HP Pavilion 14-ec1xxx", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8a20, "HP Laptop 15s-fq5xxx", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x8a25, "HP Victus 16-d1xxx (MB 8A25)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8a26, "HP Victus 16-d1xxx (MB 8A26)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8a28, "HP Envy 13", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a29, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a2a, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a2b, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a2c, "HP Envy 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a2d, "HP Envy 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a2e, "HP Envy 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a30, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a31, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8a4f, "HP Victus 15-fa0xxx (MB 8A4F)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8a6e, "HP EDNA 360", ALC287_FIXUP_CS35L41_I2C_4),
	SND_PCI_QUIRK(0x103c, 0x8a74, "HP ProBook 440 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8a75, "HP ProBook 450 G8 Notebook PC", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8a78, "HP Dev One", ALC285_FIXUP_HP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x103c, 0x8aa0, "HP ProBook 440 G9 (MB 8A9E)", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8aa3, "HP ProBook 450 G9 (MB 8AA1)", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8aa8, "HP EliteBook 640 G9 (MB 8AA6)", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8aab, "HP EliteBook 650 G9 (MB 8AA9)", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ab9, "HP EliteBook 840 G8 (MB 8AB8)", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8abb, "HP ZBook Firefly 14 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ad1, "HP EliteBook 840 14 inch G9 Notebook PC", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ad2, "HP EliteBook 860 16 inch G9 Notebook PC", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ad8, "HP 800 G9", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b0f, "HP Elite mt645 G7 Mobile Thin Client U81", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b2f, "HP 255 15.6 inch G10 Notebook PC", ALC236_FIXUP_HP_MUTE_LED_COEFBIT2),
	SND_PCI_QUIRK(0x103c, 0x8b3a, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8b3f, "HP mt440 Mobile Thin Client U91", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b42, "HP", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b43, "HP", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b44, "HP", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b45, "HP", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b46, "HP", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b47, "HP", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b59, "HP Elite mt645 G7 Mobile Thin Client U89", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b5d, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b5e, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b5f, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b63, "HP Elite Dragonfly 13.5 inch G4", ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b65, "HP ProBook 455 15.6 inch G10 Notebook PC", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b66, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b70, "HP EliteBook 835 G10", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b72, "HP EliteBook 845 G10", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b74, "HP EliteBook 845W G10", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b77, "HP ElieBook 865 G10", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8b7a, "HP", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b7d, "HP", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b87, "HP", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b8a, "HP", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b8b, "HP", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b8d, "HP", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b8f, "HP", ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b92, "HP", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8b96, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8b97, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8bb3, "HP Slim OMEN", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8bb4, "HP Slim OMEN", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8bbe, "HP Victus 16-r0xxx (MB 8BBE)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8bc8, "HP Victus 15-fa1xxx", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8bcd, "HP Omen 16-xd0xxx", ALC245_FIXUP_HP_MUTE_LED_V1_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8bd4, "HP Victus 16-s0xxx (MB 8BD4)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8bd6, "HP Pavilion Aero Laptop 13z-be200", ALC287_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8bdd, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8bde, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8bdf, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be0, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be1, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be2, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be3, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be5, "HP Envy 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be6, "HP Envy 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be7, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be8, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8be9, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8bf0, "HP", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c15, "HP Spectre x360 2-in-1 Laptop 14-eu0xxx", ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX),
	SND_PCI_QUIRK(0x103c, 0x8c16, "HP Spectre x360 2-in-1 Laptop 16-aa0xxx", ALC245_FIXUP_HP_SPECTRE_X360_16_AA0XXX),
	SND_PCI_QUIRK(0x103c, 0x8c17, "HP Spectre 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c21, "HP Pavilion Plus Laptop 14-ey0XXX", ALC245_FIXUP_HP_X360_MUTE_LEDS),
	SND_PCI_QUIRK(0x103c, 0x8c2d, "HP Victus 15-fa1xxx (MB 8C2D)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8c30, "HP Victus 15-fb1xxx", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8c46, "HP EliteBook 830 G11", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c47, "HP EliteBook 840 G11", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c48, "HP EliteBook 860 G11", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c49, "HP Elite x360 830 2-in-1 G11", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c4d, "HP Omen", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c4e, "HP Omen", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c4f, "HP Envy 15", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c50, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c51, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c52, "HP EliteBook 1040 G11", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c53, "HP Elite x360 1040 2-in-1 G11", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c66, "HP Envy 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c67, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c68, "HP Envy 17", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c6a, "HP Envy 16", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8c70, "HP EliteBook 835 G11", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c71, "HP EliteBook 845 G11", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c72, "HP EliteBook 865 G11", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c7b, "HP ProBook 445 G11", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c7c, "HP ProBook 445 G11", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c7d, "HP ProBook 465 G11", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c7e, "HP ProBook 465 G11", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c7f, "HP EliteBook 645 G11", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c80, "HP EliteBook 645 G11", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c81, "HP EliteBook 665 G11", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c89, "HP ProBook 460 G11", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c8a, "HP EliteBook 630", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c8c, "HP EliteBook 660", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c8d, "HP ProBook 440 G11", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c8e, "HP ProBook 460 G11", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c90, "HP EliteBook 640", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c91, "HP EliteBook 660", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8c96, "HP", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c97, "HP ZBook", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8c99, "HP Victus 16-r1xxx (MB 8C99)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8c9c, "HP Victus 16-s1xxx (MB 8C9C)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8ca1, "HP ZBook Power", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ca2, "HP ZBook Power", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ca4, "HP ZBook Fury", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ca7, "HP ZBook Fury", ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8caf, "HP Elite mt645 G8 Mobile Thin Client", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8cbd, "HP Pavilion Aero Laptop 13-bg0xxx", ALC245_FIXUP_HP_X360_MUTE_LEDS),
	SND_PCI_QUIRK(0x103c, 0x8cdd, "HP Spectre", ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX),
	SND_PCI_QUIRK(0x103c, 0x8cde, "HP OmniBook Ultra Flip Laptop 14t", ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX),
	SND_PCI_QUIRK(0x103c, 0x8cdf, "HP SnowWhite", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ce0, "HP SnowWhite", ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8cf5, "HP ZBook Studio 16", ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d01, "HP ZBook Power 14 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d07, "HP Victus 15-fb2xxx (MB 8D07)", ALC245_FIXUP_HP_MUTE_LED_COEFBIT),
	SND_PCI_QUIRK(0x103c, 0x8d18, "HP EliteStudio 8 AIO", ALC274_FIXUP_HP_AIO_BIND_DACS),
	SND_PCI_QUIRK(0x103c, 0x8d84, "HP EliteBook X G1i", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d85, "HP EliteBook 14 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d86, "HP Elite X360 14 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d8c, "HP EliteBook 13 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d8d, "HP Elite X360 13 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d8e, "HP EliteBook 14 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d8f, "HP EliteBook 14 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d90, "HP EliteBook 16 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d91, "HP ZBook Firefly 14 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d92, "HP ZBook Firefly 16 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8d9b, "HP 17 Turbine OmniBook 7 UMA", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8d9c, "HP 17 Turbine OmniBook 7 DIS", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8d9d, "HP 17 Turbine OmniBook X UMA", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8d9e, "HP 17 Turbine OmniBook X DIS", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8d9f, "HP 14 Cadet (x360)", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8da0, "HP 16 Clipper OmniBook 7(X360)", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8da1, "HP 16 Clipper OmniBook X", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8da7, "HP 14 Enstrom OmniBook X", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8da8, "HP 16 Piston OmniBook X", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8dd4, "HP EliteStudio 8 AIO", ALC274_FIXUP_HP_AIO_BIND_DACS),
	SND_PCI_QUIRK(0x103c, 0x8de8, "HP Gemtree", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8de9, "HP Gemtree", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8dec, "HP EliteBook 640 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8ded, "HP EliteBook 640 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8dee, "HP EliteBook 660 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8def, "HP EliteBook 660 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8df0, "HP EliteBook 630 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8df1, "HP EliteBook 630 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8dfb, "HP EliteBook 6 G1a 14", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8dfc, "HP EliteBook 645 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8dfd, "HP EliteBook 6 G1a 16", ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF),
	SND_PCI_QUIRK(0x103c, 0x8dfe, "HP EliteBook 665 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8e11, "HP Trekker", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e12, "HP Trekker", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e13, "HP Trekker", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e14, "HP ZBook Firefly 14 G12", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e15, "HP ZBook Firefly 14 G12", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e16, "HP ZBook Firefly 14 G12", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e17, "HP ZBook Firefly 14 G12", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e18, "HP ZBook Firefly 14 G12A", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e19, "HP ZBook Firefly 14 G12A", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e1a, "HP ZBook Firefly 14 G12A", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e1b, "HP EliteBook G12", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e1c, "HP EliteBook G12", ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A),
	SND_PCI_QUIRK(0x103c, 0x8e1d, "HP ZBook X Gli 16 G12", ALC236_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8e2c, "HP EliteBook 16 G12", ALC285_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x8e36, "HP 14 Enstrom OmniBook X", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e37, "HP 16 Piston OmniBook X", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e3a, "HP Agusta", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e3b, "HP Agusta", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e60, "HP Trekker ", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e61, "HP Trekker ", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8e62, "HP Trekker ", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x103c, 0x8ed5, "HP Merino13X", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8ed6, "HP Merino13", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8ed7, "HP Merino14", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8ed8, "HP Merino16", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8ed9, "HP Merino14W", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8eda, "HP Merino16W", ALC245_FIXUP_TAS2781_SPI_2),
	SND_PCI_QUIRK(0x103c, 0x8f40, "HP Lampas14", ALC287_FIXUP_TXNW2781_I2C),
	SND_PCI_QUIRK(0x103c, 0x8f41, "HP Lampas16", ALC287_FIXUP_TXNW2781_I2C),
	SND_PCI_QUIRK(0x103c, 0x8f42, "HP LampasW14", ALC287_FIXUP_TXNW2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x1032, "ASUS VivoBook X513EA", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1034, "ASUS GU605C", ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1),
	SND_PCI_QUIRK(0x1043, 0x103e, "ASUS X540SA", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x103f, "ASUS TX300", ALC282_FIXUP_ASUS_TX300),
	SND_PCI_QUIRK(0x1043, 0x1054, "ASUS G614FH/FM/FP", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x106d, "Asus K53BE", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x106f, "ASUS VivoBook X515UA", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1074, "ASUS G614PH/PM/PP", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x10a1, "ASUS UX391UA", ALC294_FIXUP_ASUS_SPK),
	SND_PCI_QUIRK(0x1043, 0x10a4, "ASUS TP3407SA", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x10c0, "ASUS X540SA", ALC256_FIXUP_ASUS_MIC),
	SND_PCI_QUIRK(0x1043, 0x10d0, "ASUS X540LA/X540LJ", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x10d3, "ASUS K6500ZC", ALC294_FIXUP_ASUS_SPK),
	SND_PCI_QUIRK(0x1043, 0x1154, "ASUS TP3607SH", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x115d, "Asus 1015E", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x1194, "ASUS UM3406KA", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x11c0, "ASUS X556UR", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1204, "ASUS Strix G615JHR_JMR_JPR", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x1214, "ASUS Strix G615LH_LM_LP", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x125e, "ASUS Q524UQK", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1271, "ASUS X430UN", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1290, "ASUS X441SA", ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1294, "ASUS B3405CVA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x12a0, "ASUS X441UV", ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x12a3, "Asus N7691ZM", ALC269_FIXUP_ASUS_N7601ZM),
	SND_PCI_QUIRK(0x1043, 0x12af, "ASUS UX582ZS", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x12b4, "ASUS B3405CCA / P3405CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x12e0, "ASUS X541SA", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x12f0, "ASUS X541UV", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1313, "Asus K42JZ", ALC269VB_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1314, "ASUS GA605K", ALC285_FIXUP_ASUS_GA605K_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x13b0, "ASUS Z550SA", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1427, "Asus Zenbook UX31E", ALC269VB_FIXUP_ASUS_ZENBOOK),
	SND_PCI_QUIRK(0x1043, 0x1433, "ASUS GX650PY/PZ/PV/PU/PYV/PZV/PIV/PVV", ALC285_FIXUP_ASUS_I2C_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1454, "ASUS PM3406CKA", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1460, "Asus VivoBook 15", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1463, "Asus GA402X/GA402N", ALC285_FIXUP_ASUS_I2C_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1473, "ASUS GU604VI/VC/VE/VG/VJ/VQ/VU/VV/VY/VZ", ALC285_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1483, "ASUS GU603VQ/VU/VV/VJ/VI", ALC285_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1493, "ASUS GV601VV/VU/VJ/VQ/VI", ALC285_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x14d3, "ASUS G614JY/JZ/JG", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x14e3, "ASUS G513PI/PU/PV", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x14f2, "ASUS VivoBook X515JA", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1503, "ASUS G733PY/PZ/PZV/PYV", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1517, "Asus Zenbook UX31A", ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A),
	SND_PCI_QUIRK(0x1043, 0x1533, "ASUS GV302XA/XJ/XQ/XU/XV/XI", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1573, "ASUS GZ301VV/VQ/VU/VJ/VA/VC/VE/VVC/VQC/VUC/VJC/VEC/VCC", ALC285_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1652, "ASUS ROG Zephyrus Do 15 SE", ALC289_FIXUP_ASUS_ZEPHYRUS_DUAL_SPK),
	SND_PCI_QUIRK(0x1043, 0x1662, "ASUS GV301QH", ALC294_FIXUP_ASUS_DUAL_SPK),
	SND_PCI_QUIRK(0x1043, 0x1663, "ASUS GU603ZI/ZJ/ZQ/ZU/ZV", ALC285_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1683, "ASUS UM3402YAR", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x16a3, "ASUS UX3402VA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x16b2, "ASUS GU603", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x16d3, "ASUS UX5304VA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x16e3, "ASUS UX50", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x16f3, "ASUS UX7602VI/BZ", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1740, "ASUS UX430UA", ALC295_FIXUP_ASUS_DACS),
	SND_PCI_QUIRK(0x1043, 0x17d1, "ASUS UX431FL", ALC294_FIXUP_ASUS_DUAL_SPK),
	SND_PCI_QUIRK(0x1043, 0x17f3, "ROG Ally NR2301L/X", ALC294_FIXUP_ASUS_ALLY),
	SND_PCI_QUIRK(0x1043, 0x1863, "ASUS UX6404VI/VV", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1881, "ASUS Zephyrus S/M", ALC294_FIXUP_ASUS_GX502_PINS),
	SND_PCI_QUIRK(0x1043, 0x18b1, "Asus MJ401TA", ALC256_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x18d3, "ASUS UM3504DA", ALC294_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x18f1, "Asus FX505DT", ALC256_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x194e, "ASUS UX563FD", ALC294_FIXUP_ASUS_HPE),
	SND_PCI_QUIRK(0x1043, 0x1970, "ASUS UX550VE", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x1982, "ASUS B1400CEPE", ALC256_FIXUP_ASUS_HPE),
	SND_PCI_QUIRK(0x1043, 0x19ce, "ASUS B9450FA", ALC294_FIXUP_ASUS_HPE),
	SND_PCI_QUIRK(0x1043, 0x19e1, "ASUS UX581LV", ALC295_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1a13, "Asus G73Jw", ALC269_FIXUP_ASUS_G73JW),
	SND_PCI_QUIRK(0x1043, 0x1a63, "ASUS UX3405MA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1a83, "ASUS UM5302LA", ALC294_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1a8e, "ASUS G712LWS", ALC294_FIXUP_LENOVO_MIC_LOCATION),
	SND_PCI_QUIRK(0x1043, 0x1a8f, "ASUS UX582ZS", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1b11, "ASUS UX431DA", ALC294_FIXUP_ASUS_COEF_1B),
	SND_PCI_QUIRK(0x1043, 0x1b13, "ASUS U41SV/GA403U", ALC285_FIXUP_ASUS_GA403U_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1b93, "ASUS G614JVR/JIR", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1bbd, "ASUS Z550MA", ALC255_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1c03, "ASUS UM3406HA", ALC294_FIXUP_ASUS_I2C_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1c23, "Asus X55U", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x1c33, "ASUS UX5304MA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1c43, "ASUS UX8406MA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1c62, "ASUS GU603", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x1c63, "ASUS GU605M", ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1),
	SND_PCI_QUIRK(0x1043, 0x1c80, "ASUS VivoBook TP401", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1c92, "ASUS ROG Strix G15", ALC285_FIXUP_ASUS_G533Z_PINS),
	SND_PCI_QUIRK(0x1043, 0x1c9f, "ASUS G614JU/JV/JI", ALC285_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1caf, "ASUS G634JY/JZ/JI/JG", ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS),
	SND_PCI_QUIRK(0x1043, 0x1ccd, "ASUS X555UB", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1ccf, "ASUS G814JU/JV/JI", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1cdf, "ASUS G814JY/JZ/JG", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1cef, "ASUS G834JY/JZ/JI/JG", ALC285_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1d1f, "ASUS G713PI/PU/PV/PVN", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1d42, "ASUS Zephyrus G14 2022", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x1d4e, "ASUS TM420", ALC256_FIXUP_ASUS_HPE),
	SND_PCI_QUIRK(0x1043, 0x1da2, "ASUS UP6502ZA/ZD", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1df3, "ASUS UM5606WA", ALC294_FIXUP_BASS_SPEAKER_15),
	SND_PCI_QUIRK(0x1043, 0x1264, "ASUS UM5606KA", ALC294_FIXUP_BASS_SPEAKER_15),
	SND_PCI_QUIRK(0x1043, 0x1e02, "ASUS UX3402ZA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1e10, "ASUS VivoBook X507UAR", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1043, 0x1e11, "ASUS Zephyrus G15", ALC289_FIXUP_ASUS_GA502),
	SND_PCI_QUIRK(0x1043, 0x1e12, "ASUS UM3402", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1e1f, "ASUS Vivobook 15 X1504VAP", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1043, 0x1e51, "ASUS Zephyrus M15", ALC294_FIXUP_ASUS_GU502_PINS),
	SND_PCI_QUIRK(0x1043, 0x1e5e, "ASUS ROG Strix G513", ALC294_FIXUP_ASUS_G513_PINS),
	SND_PCI_QUIRK(0x1043, 0x1e63, "ASUS H7606W", ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1),
	SND_PCI_QUIRK(0x1043, 0x1e83, "ASUS GA605W", ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1),
	SND_PCI_QUIRK(0x1043, 0x1e8e, "ASUS Zephyrus G15", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x1e93, "ASUS ExpertBook B9403CVAR", ALC294_FIXUP_ASUS_HPE),
	SND_PCI_QUIRK(0x1043, 0x1eb3, "ASUS Ally RCLA72", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x1ed3, "ASUS HN7306W", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1ee2, "ASUS UM6702RA/RC", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1c52, "ASUS Zephyrus G15 2022", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x1f11, "ASUS Zephyrus G14", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x1f12, "ASUS UM5302", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x1f1f, "ASUS H7604JI/JV/J3D", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1f62, "ASUS UX7602ZM", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1f63, "ASUS P5405CSA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x1f92, "ASUS ROG Flow X16", ALC289_FIXUP_ASUS_GA401),
	SND_PCI_QUIRK(0x1043, 0x1fb3, "ASUS ROG Flow Z13 GZ302EA", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x3011, "ASUS B5605CVA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x3030, "ASUS ZN270IE", ALC256_FIXUP_ASUS_AIO_GPIO2),
	SND_PCI_QUIRK(0x1043, 0x3061, "ASUS B3405CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x3071, "ASUS B5405CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x30c1, "ASUS B3605CCA / P3605CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x30d1, "ASUS B5405CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x30e1, "ASUS B5605CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x31d0, "ASUS Zen AIO 27 Z272SD_A272SD", ALC274_FIXUP_ASUS_ZEN_AIO_27),
	SND_PCI_QUIRK(0x1043, 0x31e1, "ASUS B5605CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x31f1, "ASUS B3605CCA", ALC294_FIXUP_ASUS_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x3391, "ASUS PM3606CKA", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x3a20, "ASUS G614JZR", ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS),
	SND_PCI_QUIRK(0x1043, 0x3a30, "ASUS G814JVR/JIR", ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS),
	SND_PCI_QUIRK(0x1043, 0x3a40, "ASUS G814JZR", ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS),
	SND_PCI_QUIRK(0x1043, 0x3a50, "ASUS G834JYR/JZR", ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS),
	SND_PCI_QUIRK(0x1043, 0x3a60, "ASUS G634JYR/JZR", ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS),
	SND_PCI_QUIRK(0x1043, 0x3d78, "ASUS GA603KH", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x3d88, "ASUS GA603KM", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x3e00, "ASUS G814FH/FM/FP", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x3e20, "ASUS G814PH/PM/PP", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x1043, 0x3e30, "ASUS TP3607SA", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x3ee0, "ASUS Strix G815_JHR_JMR_JPR", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x3ef0, "ASUS Strix G635LR_LW_LX", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x3f00, "ASUS Strix G815LH_LM_LP", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x3f10, "ASUS Strix G835LR_LW_LX", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x3f20, "ASUS Strix G615LR_LW", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x3f30, "ASUS Strix G815LR_LW", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x1043, 0x3fd0, "ASUS B3605CVA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x3ff0, "ASUS B5405CVA", ALC245_FIXUP_CS35L41_SPI_2),
	SND_PCI_QUIRK(0x1043, 0x831a, "ASUS P901", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x834a, "ASUS S101", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8398, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x83ce, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8516, "ASUS X101CH", ALC269_FIXUP_ASUS_X101),
	SND_PCI_QUIRK(0x1043, 0x88f4, "ASUS NUC14LNS", ALC245_FIXUP_CS35L41_SPI_1),
	SND_PCI_QUIRK(0x104d, 0x9073, "Sony VAIO", ALC275_FIXUP_SONY_VAIO_GPIO2),
	SND_PCI_QUIRK(0x104d, 0x907b, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9084, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9099, "Sony VAIO S13", ALC275_FIXUP_SONY_DISABLE_AAMIX),
	SND_PCI_QUIRK(0x104d, 0x90b5, "Sony VAIO Pro 11", ALC286_FIXUP_SONY_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x104d, 0x90b6, "Sony VAIO Pro 13", ALC286_FIXUP_SONY_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x10cf, 0x1475, "Lifebook", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x10cf, 0x159f, "Lifebook E780", ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT),
	SND_PCI_QUIRK(0x10cf, 0x15dc, "Lifebook T731", ALC269_FIXUP_LIFEBOOK_HP_PIN),
	SND_PCI_QUIRK(0x10cf, 0x1629, "Lifebook U7x7", ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC),
	SND_PCI_QUIRK(0x10cf, 0x1757, "Lifebook E752", ALC269_FIXUP_LIFEBOOK_HP_PIN),
	SND_PCI_QUIRK(0x10cf, 0x1845, "Lifebook U904", ALC269_FIXUP_LIFEBOOK_EXTMIC),
	SND_PCI_QUIRK(0x10ec, 0x10f2, "Intel Reference board", ALC700_FIXUP_INTEL_REFERENCE),
	SND_PCI_QUIRK(0x10ec, 0x118c, "Medion EE4254 MD62100", ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE),
	SND_PCI_QUIRK(0x10ec, 0x119e, "Positivo SU C1400", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x10ec, 0x11bc, "VAIO VJFE-IL", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x10ec, 0x1230, "Intel Reference board", ALC295_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x10ec, 0x124c, "Intel Reference board", ALC295_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x10ec, 0x1252, "Intel Reference board", ALC295_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x10ec, 0x1254, "Intel Reference board", ALC295_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x10ec, 0x12cc, "Intel Reference board", ALC295_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x10ec, 0x12f6, "Intel Reference board", ALC295_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x10f7, 0x8338, "Panasonic CF-SZ6", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x144d, 0xc109, "Samsung Ativ book 9 (NP900X3G)", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x144d, 0xc169, "Samsung Notebook 9 Pen (NP930SBE-K01US)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc176, "Samsung Notebook 9 Pro (NP930MBE-K04US)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc189, "Samsung Galaxy Flex Book (NT950QCG-X716)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc18a, "Samsung Galaxy Book Ion (NP930XCJ-K01US)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc1a3, "Samsung Galaxy Book Pro (NP935XDB-KC1SE)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc1a4, "Samsung Galaxy Book Pro 360 (NT935QBD)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc1a6, "Samsung Galaxy Book Pro 360 (NP930QBD)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc740, "Samsung Ativ book 8 (NP870Z5G)", ALC269_FIXUP_ATIV_BOOK_8),
	SND_PCI_QUIRK(0x144d, 0xc812, "Samsung Notebook Pen S (NT950SBE-X58)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc830, "Samsung Galaxy Book Ion (NT950XCJ-X716A)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc832, "Samsung Galaxy Book Flex Alpha (NP730QCJ)", ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET),
	SND_PCI_QUIRK(0x144d, 0xca03, "Samsung Galaxy Book2 Pro 360 (NP930QED)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xca06, "Samsung Galaxy Book3 360 (NP730QFG)", ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET),
	SND_PCI_QUIRK(0x144d, 0xc868, "Samsung Galaxy Book2 Pro (NP930XED)", ALC298_FIXUP_SAMSUNG_AMP),
	SND_PCI_QUIRK(0x144d, 0xc870, "Samsung Galaxy Book2 Pro (NP950XED)", ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS),
	SND_PCI_QUIRK(0x144d, 0xc872, "Samsung Galaxy Book2 Pro (NP950XEE)", ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS),
	SND_PCI_QUIRK(0x144d, 0xc886, "Samsung Galaxy Book3 Pro (NP964XFG)", ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS),
	SND_PCI_QUIRK(0x144d, 0xc1ca, "Samsung Galaxy Book3 Pro 360 (NP960QFG)", ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS),
	SND_PCI_QUIRK(0x144d, 0xc1cc, "Samsung Galaxy Book3 Ultra (NT960XFH)", ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS),
	SND_PCI_QUIRK(0x1458, 0xfa53, "Gigabyte BXBT-2807", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1462, 0xb120, "MSI Cubi MS-B120", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1462, 0xb171, "Cubi N 8GL (MS-B171)", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x152d, 0x1082, "Quanta NL3", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x152d, 0x1262, "Huawei NBLB-WAX9N", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1558, 0x0353, "Clevo V35[05]SN[CDE]Q", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x1323, "Clevo N130ZU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x1325, "Clevo N15[01][CW]U", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x1401, "Clevo L140[CZ]U", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x1403, "Clevo N140CU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x1404, "Clevo N150CU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x14a1, "Clevo L141MU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x2624, "Clevo L240TU", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x28c1, "Clevo V370VND", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1558, 0x35a1, "Clevo V3[56]0EN[CDE]", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x35b1, "Clevo V3[57]0WN[MNP]Q", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x4018, "Clevo NV40M[BE]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x4019, "Clevo NV40MZ", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x4020, "Clevo NV40MB", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x4041, "Clevo NV4[15]PZ", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x40a1, "Clevo NL40GU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x40c1, "Clevo NL40[CZ]U", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x40d1, "Clevo NL41DU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x5015, "Clevo NH5[58]H[HJK]Q", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x5017, "Clevo NH7[79]H[HJK]Q", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50a3, "Clevo NJ51GU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50b3, "Clevo NK50S[BEZ]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50b6, "Clevo NK50S5", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50b8, "Clevo NK50SZ", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50d5, "Clevo NP50D5", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50e1, "Clevo NH5[58]HPQ", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50e2, "Clevo NH7[79]HPQ", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50f0, "Clevo NH50A[CDF]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50f2, "Clevo NH50E[PR]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50f3, "Clevo NH58DPQ", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50f5, "Clevo NH55EPY", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x50f6, "Clevo NH55DPQ", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x5101, "Clevo S510WU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x5157, "Clevo W517GU1", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x51a1, "Clevo NS50MU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x51b1, "Clevo NS50AU", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x51b3, "Clevo NS70AU", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x5630, "Clevo NP50RNJS", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x5700, "Clevo X560WN[RST]", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x70a1, "Clevo NB70T[HJK]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x70b3, "Clevo NK70SB", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x70f2, "Clevo NH79EPY", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x70f3, "Clevo NH77DPQ", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x70f4, "Clevo NH77EPY", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x70f6, "Clevo NH77DPQ-Y", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x7716, "Clevo NS50PU", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x7717, "Clevo NS70PU", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x7718, "Clevo L140PU", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x7724, "Clevo L140AU", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8228, "Clevo NR40BU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8520, "Clevo NH50D[CD]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8521, "Clevo NH77D[CD]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8535, "Clevo NH50D[BE]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8536, "Clevo NH79D[BE]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8550, "Clevo NH[57][0-9][ER][ACDH]Q", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8551, "Clevo NH[57][0-9][ER][ACDH]Q", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8560, "Clevo NH[57][0-9][ER][ACDH]Q", ALC269_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1558, 0x8561, "Clevo NH[57][0-9][ER][ACDH]Q", ALC269_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1558, 0x8562, "Clevo NH[57][0-9]RZ[Q]", ALC269_FIXUP_DMIC),
	SND_PCI_QUIRK(0x1558, 0x8668, "Clevo NP50B[BE]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x866d, "Clevo NP5[05]PN[HJK]", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x867c, "Clevo NP7[01]PNP", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x867d, "Clevo NP7[01]PN[HJK]", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8680, "Clevo NJ50LU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8686, "Clevo NH50[CZ]U", ALC256_FIXUP_MIC_NO_PRESENCE_AND_RESUME),
	SND_PCI_QUIRK(0x1558, 0x8a20, "Clevo NH55DCQ-Y", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8a51, "Clevo NH70RCQ-Y", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x8d50, "Clevo NH55RCQ-M", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x951d, "Clevo N950T[CDF]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x9600, "Clevo N960K[PR]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x961d, "Clevo N960S[CDF]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0x971d, "Clevo N970T[CDF]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xa500, "Clevo NL5[03]RU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xa554, "VAIO VJFH52", ALC269_FIXUP_VAIO_VJFH52_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xa559, "VAIO RPL", ALC256_FIXUP_VAIO_RPL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xa600, "Clevo NL50NU", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xa650, "Clevo NP[567]0SN[CD]", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xa671, "Clevo NP70SN[CDE]", ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xa741, "Clevo V54x_6x_TNE", ALC245_FIXUP_CLEVO_NOISY_MIC),
	SND_PCI_QUIRK(0x1558, 0xa743, "Clevo V54x_6x_TU", ALC245_FIXUP_CLEVO_NOISY_MIC),
	SND_PCI_QUIRK(0x1558, 0xa763, "Clevo V54x_6x_TU", ALC245_FIXUP_CLEVO_NOISY_MIC),
	SND_PCI_QUIRK(0x1558, 0xb018, "Clevo NP50D[BE]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xb019, "Clevo NH77D[BE]Q", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xb022, "Clevo NH77D[DC][QW]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xc018, "Clevo NP50D[BE]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xc019, "Clevo NH77D[BE]Q", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1558, 0xc022, "Clevo NH77[DC][QW]", ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x17aa, 0x1036, "Lenovo P520", ALC233_FIXUP_LENOVO_MULTI_CODECS),
	SND_PCI_QUIRK(0x17aa, 0x1048, "ThinkCentre Station", ALC623_FIXUP_LENOVO_THINKSTATION_P340),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Thinkpad SL410/510", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Thinkpad L512", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21b8, "Thinkpad Edge 14", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21ca, "Thinkpad L412", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21e9, "Thinkpad Edge 15", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21f3, "Thinkpad T430", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x21f6, "Thinkpad T530", ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x21fa, "Thinkpad X230", ALC269_FIXUP_LENOVO_DOCK),
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
	SND_PCI_QUIRK(0x17aa, 0x2234, "Thinkpad ICE-1", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x2245, "Thinkpad T470", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2246, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2247, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2249, "Thinkpad", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x224b, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x224c, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x224d, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x225d, "Thinkpad T480", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x2292, "Thinkpad X1 Carbon 7th", ALC285_FIXUP_THINKPAD_HEADSET_JACK),
	SND_PCI_QUIRK(0x17aa, 0x22be, "Thinkpad X1 Carbon 8th", ALC285_FIXUP_THINKPAD_HEADSET_JACK),
	SND_PCI_QUIRK(0x17aa, 0x22c1, "Thinkpad P1 Gen 3", ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK),
	SND_PCI_QUIRK(0x17aa, 0x22c2, "Thinkpad X1 Extreme Gen 3", ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK),
	SND_PCI_QUIRK(0x17aa, 0x22f1, "Thinkpad", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x22f2, "Thinkpad", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x22f3, "Thinkpad", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x2316, "Thinkpad P1 Gen 6", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x2317, "Thinkpad P1 Gen 6", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x2318, "Thinkpad Z13 Gen2", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x2319, "Thinkpad Z16 Gen2", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x231a, "Thinkpad Z16 Gen2", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x231e, "Thinkpad", ALC287_FIXUP_LENOVO_THKPAD_WH_ALC1318),
	SND_PCI_QUIRK(0x17aa, 0x231f, "Thinkpad", ALC287_FIXUP_LENOVO_THKPAD_WH_ALC1318),
	SND_PCI_QUIRK(0x17aa, 0x2326, "Hera2", ALC287_FIXUP_TAS2781_I2C),
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
	SND_PCI_QUIRK(0x17aa, 0x31af, "ThinkCentre Station", ALC623_FIXUP_LENOVO_THINKSTATION_P340),
	SND_PCI_QUIRK(0x17aa, 0x334b, "Lenovo ThinkCentre M70 Gen5", ALC283_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3384, "ThinkCentre M90a PRO", ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED),
	SND_PCI_QUIRK(0x17aa, 0x3386, "ThinkCentre M90a Gen6", ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED),
	SND_PCI_QUIRK(0x17aa, 0x3387, "ThinkCentre M70a Gen6", ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED),
	SND_PCI_QUIRK(0x17aa, 0x3801, "Lenovo Yoga9 14IAP7", ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN),
	HDA_CODEC_QUIRK(0x17aa, 0x3802, "DuetITL 2021", ALC287_FIXUP_YOGA7_14ITL_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3802, "Lenovo Yoga Pro 9 14IRP8", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3813, "Legion 7i 15IMHG05", ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3818, "Lenovo C940 / Yoga Duet 7", ALC298_FIXUP_LENOVO_C940_DUET7),
	SND_PCI_QUIRK(0x17aa, 0x3819, "Lenovo 13s Gen2 ITL", ALC287_FIXUP_13S_GEN2_SPEAKERS),
	HDA_CODEC_QUIRK(0x17aa, 0x3820, "IdeaPad 330-17IKB 81DM", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3820, "Yoga Duet 7 13ITL6", ALC287_FIXUP_YOGA7_14ITL_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3824, "Legion Y9000X 2020", ALC285_FIXUP_LEGION_Y9000X_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3827, "Ideapad S740", ALC285_FIXUP_IDEAPAD_S740_COEF),
	SND_PCI_QUIRK(0x17aa, 0x3834, "Lenovo IdeaPad Slim 9i 14ITL5", ALC287_FIXUP_YOGA7_14ITL_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x383d, "Legion Y9000X 2019", ALC285_FIXUP_LEGION_Y9000X_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3843, "Yoga 9i", ALC287_FIXUP_IDEAPAD_BASS_SPK_AMP),
	SND_PCI_QUIRK(0x17aa, 0x3847, "Legion 7 16ACHG6", ALC287_FIXUP_LEGION_16ACHG6),
	SND_PCI_QUIRK(0x17aa, 0x384a, "Lenovo Yoga 7 15ITL5", ALC287_FIXUP_YOGA7_14ITL_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3852, "Lenovo Yoga 7 14ITL5", ALC287_FIXUP_YOGA7_14ITL_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3853, "Lenovo Yoga 7 15ITL5", ALC287_FIXUP_YOGA7_14ITL_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x3855, "Legion 7 16ITHG6", ALC287_FIXUP_LEGION_16ITHG6),
	SND_PCI_QUIRK(0x17aa, 0x3865, "Lenovo 13X", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x3866, "Lenovo 13X", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x3869, "Lenovo Yoga7 14IAL7", ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN),
	HDA_CODEC_QUIRK(0x17aa, 0x386e, "Legion Y9000X 2022 IAH7", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x386e, "Yoga Pro 7 14ARP8", ALC285_FIXUP_SPEAKER2_TO_DAC1),
	HDA_CODEC_QUIRK(0x17aa, 0x38a8, "Legion Pro 7 16ARX8H", ALC287_FIXUP_TAS2781_I2C), /* this must match before PCI SSID 17aa:386f below */
	SND_PCI_QUIRK(0x17aa, 0x386f, "Legion Pro 7i 16IAX7", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x3870, "Lenovo Yoga 7 14ARB7", ALC287_FIXUP_YOGA7_14ARB7_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3877, "Lenovo Legion 7 Slim 16ARHA7", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x3878, "Lenovo Legion 7 Slim 16ARHA7", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x387d, "Yoga S780-16 pro Quad AAC", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x387e, "Yoga S780-16 pro Quad YC", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x387f, "Yoga S780-16 pro dual LX", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3880, "Yoga S780-16 pro dual YC", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3881, "YB9 dual power mode2 YC", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3882, "Lenovo Yoga Pro 7 14APH8", ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN),
	SND_PCI_QUIRK(0x17aa, 0x3884, "Y780 YG DUAL", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3886, "Y780 VECO DUAL", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3891, "Lenovo Yoga Pro 7 14AHP9", ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN),
	SND_PCI_QUIRK(0x17aa, 0x38a5, "Y580P AMD dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38a7, "Y780P AMD YG dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38a8, "Y780P AMD VECO dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38a9, "Thinkbook 16P", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x38ab, "Thinkbook 16P", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x38b4, "Legion Slim 7 16IRH8", ALC287_FIXUP_CS35L41_I2C_2),
	HDA_CODEC_QUIRK(0x17aa, 0x391c, "Lenovo Yoga 7 2-in-1 14AKP10", ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN),
	SND_PCI_QUIRK(0x17aa, 0x38b5, "Legion Slim 7 16IRH8", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x38b6, "Legion Slim 7 16APH8", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x38b7, "Legion Slim 7 16APH8", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x17aa, 0x38b8, "Yoga S780-14.5 proX AMD YC Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38b9, "Yoga S780-14.5 proX AMD LX Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38ba, "Yoga S780-14.5 Air AMD quad YC", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38bb, "Yoga S780-14.5 Air AMD quad AAC", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38be, "Yoga S980-14.5 proX YC Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38bf, "Yoga S980-14.5 proX LX Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38c3, "Y980 DUAL", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38c7, "Thinkbook 13x Gen 4", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x38c8, "Thinkbook 13x Gen 4", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x38cb, "Y790 YG DUAL", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38cd, "Y790 VECO DUAL", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38d2, "Lenovo Yoga 9 14IMH9", ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN),
	SND_PCI_QUIRK(0x17aa, 0x38d3, "Yoga S990-16 Pro IMH YC Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38d4, "Yoga S990-16 Pro IMH VECO Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38d5, "Yoga S990-16 Pro IMH YC Quad", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38d6, "Yoga S990-16 Pro IMH VECO Quad", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38d7, "Lenovo Yoga 9 14IMH9", ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN),
	SND_PCI_QUIRK(0x17aa, 0x38df, "Yoga Y990 Intel YC Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38e0, "Yoga Y990 Intel VECO Dual", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38f8, "Yoga Book 9i", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38df, "Y990 YG DUAL", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x38f9, "Thinkbook 16P Gen5", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x38fa, "Thinkbook 16P Gen5", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x38fd, "ThinkBook plus Gen5 Hybrid", ALC287_FIXUP_TAS2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3902, "Lenovo E50-80", ALC269_FIXUP_DMIC_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x17aa, 0x390d, "Lenovo Yoga Pro 7 14ASP10", ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN),
	SND_PCI_QUIRK(0x17aa, 0x3913, "Lenovo 145", ALC236_FIXUP_LENOVO_INV_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x391f, "Yoga S990-16 pro Quad YC Quad", ALC287_FIXUP_TXNW2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3920, "Yoga S990-16 pro Quad VECO Quad", ALC287_FIXUP_TXNW2781_I2C),
	SND_PCI_QUIRK(0x17aa, 0x3929, "Thinkbook 13x Gen 5", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x392b, "Thinkbook 13x Gen 5", ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x3977, "IdeaPad S210", ALC283_FIXUP_INT_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3978, "Lenovo B50-70", ALC269_FIXUP_DMIC_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_FIXUP_PCM_44K),
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
	SND_PCI_QUIRK(0x17aa, 0x508b, "Thinkpad X12 Gen 1", ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS),
	SND_PCI_QUIRK(0x17aa, 0x5109, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x511e, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x511f, "Thinkpad", ALC298_FIXUP_TPT470_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x9e54, "LENOVO NB", ALC269_FIXUP_LENOVO_EAPD),
	SND_PCI_QUIRK(0x17aa, 0x9e56, "Lenovo ZhaoYang CF4620Z", ALC286_FIXUP_SONY_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1849, 0x0269, "Positivo Master C6400", ALC269VB_FIXUP_ASUS_ZENBOOK),
	SND_PCI_QUIRK(0x1849, 0x1233, "ASRock NUC Box 1100", ALC233_FIXUP_NO_AUDIO_JACK),
	SND_PCI_QUIRK(0x1849, 0xa233, "Positivo Master C6300", ALC269_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1854, 0x0440, "LG CQ6", ALC256_FIXUP_HEADPHONE_AMP_VOL),
	SND_PCI_QUIRK(0x1854, 0x0441, "LG CQ6 AIO", ALC256_FIXUP_HEADPHONE_AMP_VOL),
	SND_PCI_QUIRK(0x1854, 0x0488, "LG gram 16 (16Z90R)", ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS),
	SND_PCI_QUIRK(0x1854, 0x0489, "LG gram 16 (16Z90R-A)", ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS),
	SND_PCI_QUIRK(0x1854, 0x048a, "LG gram 17 (17ZD90R)", ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS),
	SND_PCI_QUIRK(0x19e5, 0x3204, "Huawei MACH-WX9", ALC256_FIXUP_HUAWEI_MACH_WX9_PINS),
	SND_PCI_QUIRK(0x19e5, 0x320f, "Huawei WRT-WX9 ", ALC256_FIXUP_ASUS_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x19e5, 0x3212, "Huawei KLV-WX9 ", ALC256_FIXUP_ACER_HEADSET_MIC),
	SND_PCI_QUIRK(0x1b35, 0x1235, "CZC B20", ALC269_FIXUP_CZC_B20),
	SND_PCI_QUIRK(0x1b35, 0x1236, "CZC TMI", ALC269_FIXUP_CZC_TMI),
	SND_PCI_QUIRK(0x1b35, 0x1237, "CZC L101", ALC269_FIXUP_CZC_L101),
	SND_PCI_QUIRK(0x1b7d, 0xa831, "Ordissimo EVE2 ", ALC269VB_FIXUP_ORDISSIMO_EVE2), /* Also known as Malata PC-B1303 */
	SND_PCI_QUIRK(0x1c06, 0x2013, "Lemote A1802", ALC269_FIXUP_LEMOTE_A1802),
	SND_PCI_QUIRK(0x1c06, 0x2015, "Lemote A190X", ALC269_FIXUP_LEMOTE_A190X),
	SND_PCI_QUIRK(0x1c6c, 0x122a, "Positivo N14AP7", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1c6c, 0x1251, "Positivo N14KP6-TG", ALC288_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1d05, 0x1132, "TongFang PHxTxX1", ALC256_FIXUP_SET_COEF_DEFAULTS),
	SND_PCI_QUIRK(0x1d05, 0x1096, "TongFang GMxMRxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x1100, "TongFang GKxNRxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x1111, "TongFang GMxZGxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x1119, "TongFang GMxZGxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x1129, "TongFang GMxZGxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x1147, "TongFang GMxTGxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x115c, "TongFang GMxTGxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x121b, "TongFang GMxAGxx", ALC269_FIXUP_NO_SHUTUP),
	SND_PCI_QUIRK(0x1d05, 0x1387, "TongFang GMxIXxx", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1d05, 0x1409, "TongFang GMxIXxx", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1d05, 0x300f, "TongFang X6AR5xxY", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1d05, 0x3019, "TongFang X6FR5xxY", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1d17, 0x3288, "Haier Boyue G42", ALC269VC_FIXUP_ACER_VCOPPERBOX_PINS),
	SND_PCI_QUIRK(0x1d72, 0x1602, "RedmiBook", ALC255_FIXUP_XIAOMI_HEADSET_MIC),
	SND_PCI_QUIRK(0x1d72, 0x1701, "XiaomiNotebook Pro", ALC298_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1d72, 0x1901, "RedmiBook 14", ALC256_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1d72, 0x1945, "Redmi G", ALC256_FIXUP_ASUS_HEADSET_MIC),
	SND_PCI_QUIRK(0x1d72, 0x1947, "RedmiBook Air", ALC255_FIXUP_XIAOMI_HEADSET_MIC),
	SND_PCI_QUIRK(0x1ee7, 0x2078, "HONOR BRB-X M1010", ALC2XX_FIXUP_HEADSET_MIC),
	SND_PCI_QUIRK(0x1f66, 0x0105, "Ayaneo Portable Game Player", ALC287_FIXUP_CS35L41_I2C_2),
	SND_PCI_QUIRK(0x2014, 0x800a, "Positivo ARN50", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x2782, 0x0214, "VAIO VJFE-CL", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x2782, 0x0228, "Infinix ZERO BOOK 13", ALC269VB_FIXUP_INFINIX_ZERO_BOOK_13),
	SND_PCI_QUIRK(0x2782, 0x0232, "CHUWI CoreBook XPro", ALC269VB_FIXUP_CHUWI_COREBOOK_XPRO),
	SND_PCI_QUIRK(0x2782, 0x1407, "Positivo P15X", ALC269_FIXUP_POSITIVO_P15X_HEADSET_MIC),
	SND_PCI_QUIRK(0x2782, 0x1409, "Positivo K116J", ALC269_FIXUP_POSITIVO_P15X_HEADSET_MIC),
	SND_PCI_QUIRK(0x2782, 0x1701, "Infinix Y4 Max", ALC269VC_FIXUP_INFINIX_Y4_MAX),
	SND_PCI_QUIRK(0x2782, 0x1705, "MEDION E15433", ALC269VC_FIXUP_INFINIX_Y4_MAX),
	SND_PCI_QUIRK(0x2782, 0x1707, "Vaio VJFE-ADL", ALC298_FIXUP_SPK_VOLUME),
	SND_PCI_QUIRK(0x2782, 0x4900, "MEDION E15443", ALC233_FIXUP_MEDION_MTL_SPK),
	SND_PCI_QUIRK(0x8086, 0x2074, "Intel NUC 8", ALC233_FIXUP_INTEL_NUC8_DMIC),
	SND_PCI_QUIRK(0x8086, 0x2080, "Intel NUC 8 Rugged", ALC256_FIXUP_INTEL_NUC8_RUGGED),
	SND_PCI_QUIRK(0x8086, 0x2081, "Intel NUC 10", ALC256_FIXUP_INTEL_NUC10),
	SND_PCI_QUIRK(0x8086, 0x3038, "Intel NUC 13", ALC295_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0xf111, 0x0001, "Framework Laptop", ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0xf111, 0x0006, "Framework Laptop", ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0xf111, 0x0009, "Framework Laptop", ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0xf111, 0x000b, "Framework Laptop", ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0xf111, 0x000c, "Framework Laptop", ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE),

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

static const struct hda_quirk alc269_fixup_vendor_tbl[] = {
	SND_PCI_QUIRK_VENDOR(0x1025, "Acer Aspire", ALC271_FIXUP_DMIC),
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK_VENDOR(0x104d, "Sony VAIO", ALC269_FIXUP_SONY_VAIO),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Lenovo XPAD", ALC269_FIXUP_LENOVO_XPAD_ACPI),
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
	{.id = ALC269_FIXUP_DELL4_MIC_NO_PRESENCE_QUIET, .name = "dell-headset4-quiet"},
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
	{.id = ALC269_FIXUP_LENOVO_XPAD_ACPI, .name = "lenovo-xpad-led"},
	{.id = ALC269_FIXUP_DMIC_THINKPAD_ACPI, .name = "dmic-thinkpad"},
	{.id = ALC255_FIXUP_ACER_MIC_NO_PRESENCE, .name = "alc255-acer"},
	{.id = ALC255_FIXUP_ASUS_MIC_NO_PRESENCE, .name = "alc255-asus"},
	{.id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "alc255-dell1"},
	{.id = ALC255_FIXUP_DELL2_MIC_NO_PRESENCE, .name = "alc255-dell2"},
	{.id = ALC293_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "alc293-dell1"},
	{.id = ALC283_FIXUP_HEADSET_MIC, .name = "alc283-headset"},
	{.id = ALC255_FIXUP_MIC_MUTE_LED, .name = "alc255-dell-mute"},
	{.id = ALC282_FIXUP_ASPIRE_V5_PINS, .name = "aspire-v5"},
	{.id = ALC269VB_FIXUP_ASPIRE_E1_COEF, .name = "aspire-e1-coef"},
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
	{.id = ALC256_FIXUP_CHROME_BOOK, .name = "alc-2024y-chromebook"},
	{.id = ALC299_FIXUP_PREDATOR_SPK, .name = "predator-spk"},
	{.id = ALC298_FIXUP_HUAWEI_MBX_STEREO, .name = "huawei-mbx-stereo"},
	{.id = ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE, .name = "alc256-medion-headset"},
	{.id = ALC298_FIXUP_SAMSUNG_AMP, .name = "alc298-samsung-amp"},
	{.id = ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS, .name = "alc298-samsung-amp-v2-2-amps"},
	{.id = ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS, .name = "alc298-samsung-amp-v2-4-amps"},
	{.id = ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET, .name = "alc256-samsung-headphone"},
	{.id = ALC255_FIXUP_XIAOMI_HEADSET_MIC, .name = "alc255-xiaomi-headset"},
	{.id = ALC274_FIXUP_HP_MIC, .name = "alc274-hp-mic-detect"},
	{.id = ALC245_FIXUP_HP_X360_AMP, .name = "alc245-hp-x360-amp"},
	{.id = ALC295_FIXUP_HP_OMEN, .name = "alc295-hp-omen"},
	{.id = ALC285_FIXUP_HP_SPECTRE_X360, .name = "alc285-hp-spectre-x360"},
	{.id = ALC285_FIXUP_HP_SPECTRE_X360_EB1, .name = "alc285-hp-spectre-x360-eb1"},
	{.id = ALC285_FIXUP_HP_SPECTRE_X360_DF1, .name = "alc285-hp-spectre-x360-df1"},
	{.id = ALC285_FIXUP_HP_ENVY_X360, .name = "alc285-hp-envy-x360"},
	{.id = ALC287_FIXUP_IDEAPAD_BASS_SPK_AMP, .name = "alc287-ideapad-bass-spk-amp"},
	{.id = ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN, .name = "alc287-yoga9-bass-spk-pin"},
	{.id = ALC623_FIXUP_LENOVO_THINKSTATION_P340, .name = "alc623-lenovo-thinkstation-p340"},
	{.id = ALC255_FIXUP_ACER_HEADPHONE_AND_MIC, .name = "alc255-acer-headphone-and-mic"},
	{.id = ALC285_FIXUP_HP_GPIO_AMP_INIT, .name = "alc285-hp-amp-init"},
	{.id = ALC236_FIXUP_LENOVO_INV_DMIC, .name = "alc236-fixup-lenovo-inv-mic"},
	{.id = ALC2XX_FIXUP_HEADSET_MIC, .name = "alc2xx-fixup-headset-mic"},
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
	SND_HDA_PIN_QUIRK(0x10ec0236, 0x1028, "Dell", ALC236_FIXUP_DELL_AIO_HEADSET_MIC,
		{0x21, 0x02211010}),
	SND_HDA_PIN_QUIRK(0x10ec0236, 0x103c, "HP", ALC256_FIXUP_HP_HEADSET_MIC,
		{0x14, 0x90170110},
		{0x19, 0x02a11020},
		{0x21, 0x02211030}),
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
       SND_HDA_PIN_QUIRK(0x10ec0256, 0x103c, "HP", ALC256_FIXUP_HP_HEADSET_MIC,
		{0x14, 0x90170110},
		{0x19, 0x02a11020},
		{0x21, 0x0221101f}),
       SND_HDA_PIN_QUIRK(0x10ec0274, 0x103c, "HP", ALC274_FIXUP_HP_HEADSET_MIC,
		{0x17, 0x90170110},
		{0x19, 0x03a11030},
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
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x1025, "Acer", ALC282_FIXUP_ACER_DISABLE_LINEOUT,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a609c0},
		{0x18, 0x03a11830},
		{0x19, 0x04a19831},
		{0x1a, 0x0481303f},
		{0x1b, 0x04211020},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x1025, "Acer", ALC282_FIXUP_ACER_DISABLE_LINEOUT,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a60940},
		{0x18, 0x03a11830},
		{0x19, 0x04a19831},
		{0x1a, 0x0481303f},
		{0x1b, 0x04211020},
		{0x21, 0x0321101f}),
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
	SND_HDA_PIN_QUIRK(0x10ec0285, 0x17aa, "Lenovo", ALC285_FIXUP_LENOVO_PC_BEEP_IN_NOISE,
		{0x14, 0x90170110},
		{0x19, 0x04a11040},
		{0x1d, 0x40600001},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0285, 0x17aa, "Lenovo", ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK,
		{0x14, 0x90170110},
		{0x19, 0x04a11040},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0287, 0x17aa, "Lenovo", ALC285_FIXUP_THINKPAD_HEADSET_JACK,
		{0x14, 0x90170110},
		{0x17, 0x90170111},
		{0x19, 0x03a11030},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0287, 0x17aa, "Lenovo", ALC287_FIXUP_THINKPAD_I2S_SPK,
		{0x17, 0x90170110},
		{0x19, 0x03a11030},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0287, 0x17aa, "Lenovo", ALC287_FIXUP_THINKPAD_I2S_SPK,
		{0x17, 0x90170110}, /* 0x231f with RTK I2S AMP */
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
	SND_HDA_PIN_QUIRK(0x10ec0294, 0x1043, "ASUS", ALC294_FIXUP_ASUS_HPE,
		{0x17, 0x90170110},
		{0x21, 0x04211020}),
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
	SND_HDA_PIN_QUIRK(0x10ec0623, 0x17aa, "Lenovo", ALC283_FIXUP_HEADSET_MIC,
		{0x14, 0x01014010},
		{0x17, 0x90170120},
		{0x18, 0x02a11030},
		{0x19, 0x02a1103f},
		{0x21, 0x0221101f}),
	{}
};

/* This is the fallback pin_fixup_tbl for alc269 family, to make the tbl match
 * more machines, don't need to match all valid pins, just need to match
 * all the pins defined in the tbl. Just because of this reason, it is possible
 * that a single machine matches multiple tbls, so there is one limitation:
 *   at most one tbl is allowed to define for the same vendor and same codec
 */
static const struct snd_hda_pin_quirk alc269_fallback_pin_fixup_tbl[] = {
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1025, "Acer", ALC2XX_FIXUP_HEADSET_MIC,
		{0x19, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0289, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
		{0x19, 0x40000000},
		{0x1b, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE_QUIET,
		{0x19, 0x40000000},
		{0x1b, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x19, 0x40000000},
		{0x1a, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0236, 0x1028, "Dell", ALC255_FIXUP_DELL1_LIMIT_INT_MIC_BOOST,
		{0x19, 0x40000000},
		{0x1a, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0274, 0x1028, "Dell", ALC269_FIXUP_DELL1_LIMIT_INT_MIC_BOOST,
		{0x19, 0x40000000},
		{0x1a, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1043, "ASUS", ALC2XX_FIXUP_HEADSET_MIC,
		{0x19, 0x40000000}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1558, "Clevo", ALC2XX_FIXUP_HEADSET_MIC,
		{0x19, 0x40000000}),
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

static void alc269_remove(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec)
		hda_component_manager_free(&spec->comps, &comp_master_ops);

	snd_hda_gen_remove(codec);
}

/*
 */
static int alc269_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.shared_mic_vref_pin = 0x18;
	codec->power_save_node = 0;
	spec->en_3kpull_low = true;

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
	case 0x10ec0230:
	case 0x10ec0236:
	case 0x10ec0256:
	case 0x19e58326:
		spec->codec_variant = ALC269_TYPE_ALC256;
		spec->shutup = alc256_shutup;
		spec->init_hook = alc256_init;
		spec->gen.mixer_nid = 0; /* ALC256 does not have any loopback mixer path */
		if (codec->core.vendor_id == 0x10ec0236 &&
		    codec->bus->pci->vendor != PCI_VENDOR_ID_AMD)
			spec->en_3kpull_low = false;
		break;
	case 0x10ec0257:
		spec->codec_variant = ALC269_TYPE_ALC257;
		spec->shutup = alc256_shutup;
		spec->init_hook = alc256_init;
		spec->gen.mixer_nid = 0;
		spec->en_3kpull_low = false;
		break;
	case 0x10ec0215:
	case 0x10ec0245:
	case 0x10ec0285:
	case 0x10ec0289:
		if (alc_get_coef0(codec) & 0x0010)
			spec->codec_variant = ALC269_TYPE_ALC245;
		else
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
	case 0x10ec0287:
		spec->codec_variant = ALC269_TYPE_ALC287;
		spec->shutup = alc225_shutup;
		spec->init_hook = alc225_init;
		spec->gen.mixer_nid = 0; /* no loopback on ALC287 */
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
	case 0x10ec0222:
	case 0x10ec0623:
		spec->codec_variant = ALC269_TYPE_ALC623;
		spec->shutup = alc222_shutup;
		spec->init_hook = alc222_init;
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
	/* FIXME: both TX300 and ROG Strix G17 have the same SSID, and
	 * the quirk breaks the latter (bko#214101).
	 * Clear the wrong entry.
	 */
	if (codec->fixup_id == ALC282_FIXUP_ASUS_TX300 &&
	    codec->core.vendor_id == 0x10ec0294) {
		codec_dbg(codec, "Clear wrong fixup for ASUS ROG Strix G17\n");
		codec->fixup_id = HDA_FIXUP_ID_NOT_SET;
	}

	snd_hda_pick_pin_fixup(codec, alc269_pin_fixup_tbl, alc269_fixups, true);
	snd_hda_pick_pin_fixup(codec, alc269_fallback_pin_fixup_tbl, alc269_fixups, false);
	snd_hda_pick_fixup(codec, NULL,	alc269_fixup_vendor_tbl,
			   alc269_fixups);

	/*
	 * Check whether ACPI describes companion amplifiers that require
	 * component binding
	 */
	find_cirrus_companion_amps(codec);

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
	alc269_remove(codec);
	return err;
}

static const struct hda_codec_ops alc269_codec_ops = {
	.probe = alc269_probe,
	.remove = alc269_remove,
	.build_controls = alc_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = alc_init,
	.unsol_event = snd_hda_jack_unsol_event,
	.suspend = alc269_suspend,
	.resume = alc269_resume,
	.check_power_status = snd_hda_gen_check_power_status,
	.stream_pm = snd_hda_gen_stream_pm,
};

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_alc269[] = {
	HDA_CODEC_ID(0x10ec0215, "ALC215"),
	HDA_CODEC_ID(0x10ec0221, "ALC221"),
	HDA_CODEC_ID(0x10ec0222, "ALC222"),
	HDA_CODEC_ID(0x10ec0225, "ALC225"),
	HDA_CODEC_ID(0x10ec0230, "ALC236"),
	HDA_CODEC_ID(0x10ec0231, "ALC231"),
	HDA_CODEC_ID(0x10ec0233, "ALC233"),
	HDA_CODEC_ID(0x10ec0234, "ALC234"),
	HDA_CODEC_ID(0x10ec0235, "ALC233"),
	HDA_CODEC_ID(0x10ec0236, "ALC236"),
	HDA_CODEC_ID(0x10ec0245, "ALC245"),
	HDA_CODEC_ID(0x10ec0255, "ALC255"),
	HDA_CODEC_ID(0x10ec0256, "ALC256"),
	HDA_CODEC_ID(0x10ec0257, "ALC257"),
	HDA_CODEC_ID(0x10ec0269, "ALC269"),
	HDA_CODEC_ID(0x10ec0270, "ALC270"),
	HDA_CODEC_ID(0x10ec0274, "ALC274"),
	HDA_CODEC_ID(0x10ec0275, "ALC275"),
	HDA_CODEC_ID(0x10ec0276, "ALC276"),
	HDA_CODEC_ID(0x10ec0280, "ALC280"),
	HDA_CODEC_ID(0x10ec0282, "ALC282"),
	HDA_CODEC_ID(0x10ec0283, "ALC283"),
	HDA_CODEC_ID(0x10ec0284, "ALC284"),
	HDA_CODEC_ID(0x10ec0285, "ALC285"),
	HDA_CODEC_ID(0x10ec0286, "ALC286"),
	HDA_CODEC_ID(0x10ec0287, "ALC287"),
	HDA_CODEC_ID(0x10ec0288, "ALC288"),
	HDA_CODEC_ID(0x10ec0289, "ALC289"),
	HDA_CODEC_ID(0x10ec0290, "ALC290"),
	HDA_CODEC_ID(0x10ec0292, "ALC292"),
	HDA_CODEC_ID(0x10ec0293, "ALC293"),
	HDA_CODEC_ID(0x10ec0294, "ALC294"),
	HDA_CODEC_ID(0x10ec0295, "ALC295"),
	HDA_CODEC_ID(0x10ec0298, "ALC298"),
	HDA_CODEC_ID(0x10ec0299, "ALC299"),
	HDA_CODEC_ID(0x10ec0300, "ALC300"),
	HDA_CODEC_ID(0x10ec0623, "ALC623"),
	HDA_CODEC_ID(0x10ec0700, "ALC700"),
	HDA_CODEC_ID(0x10ec0701, "ALC701"),
	HDA_CODEC_ID(0x10ec0703, "ALC703"),
	HDA_CODEC_ID(0x10ec0711, "ALC711"),
	HDA_CODEC_ID(0x19e58326, "HW8326"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_alc269);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek ALC269 and compatible HD-audio codecs");
MODULE_IMPORT_NS("SND_HDA_CODEC_REALTEK");
MODULE_IMPORT_NS("SND_HDA_SCODEC_COMPONENT");

static struct hda_codec_driver alc269_driver = {
	.id = snd_hda_id_alc269,
	.ops = &alc269_codec_ops,
};

module_hda_codec_driver(alc269_driver);
