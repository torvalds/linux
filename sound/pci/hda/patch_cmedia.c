// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for C-Media CMI9880
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/* CM9825 Offset Definitions */

#define CM9825_VERB_SET_HPF_1 0x781
#define CM9825_VERB_SET_HPF_2 0x785
#define CM9825_VERB_SET_PLL 0x7a0
#define CM9825_VERB_SET_NEG 0x7a1
#define CM9825_VERB_SET_ADCL 0x7a2
#define CM9825_VERB_SET_DACL 0x7a3
#define CM9825_VERB_SET_MBIAS 0x7a4
#define CM9825_VERB_SET_VNEG 0x7a8
#define CM9825_VERB_SET_D2S 0x7a9
#define CM9825_VERB_SET_DACTRL 0x7aa
#define CM9825_VERB_SET_PDNEG 0x7ac
#define CM9825_VERB_SET_VDO 0x7ad
#define CM9825_VERB_SET_CDALR 0x7b0
#define CM9825_VERB_SET_MTCBA 0x7b1
#define CM9825_VERB_SET_OTP 0x7b2
#define CM9825_VERB_SET_OCP 0x7b3
#define CM9825_VERB_SET_GAD 0x7b4
#define CM9825_VERB_SET_TMOD 0x7b5
#define CM9825_VERB_SET_SNR 0x7b6

struct cmi_spec {
	struct hda_gen_spec gen;
	const struct hda_verb *chip_d0_verbs;
	const struct hda_verb *chip_d3_verbs;
	const struct hda_verb *chip_hp_present_verbs;
	const struct hda_verb *chip_hp_remove_verbs;
	struct hda_codec *codec;
	struct delayed_work unsol_hp_work;
	int quirk;
};

static const struct hda_verb cm9825_std_d3_verbs[] = {
	/* chip sleep verbs */
	{0x43, CM9825_VERB_SET_D2S, 0x62},	/* depop */
	{0x43, CM9825_VERB_SET_PLL, 0x01},	/* PLL set */
	{0x43, CM9825_VERB_SET_NEG, 0xc2},	/* NEG set */
	{0x43, CM9825_VERB_SET_ADCL, 0x00},	/* ADC */
	{0x43, CM9825_VERB_SET_DACL, 0x02},	/* DACL */
	{0x43, CM9825_VERB_SET_VNEG, 0x50},	/* VOL NEG */
	{0x43, CM9825_VERB_SET_MBIAS, 0x00},	/* MBIAS */
	{0x43, CM9825_VERB_SET_PDNEG, 0x04},	/* SEL OSC */
	{0x43, CM9825_VERB_SET_CDALR, 0xf6},	/* Class D */
	{0x43, CM9825_VERB_SET_OTP, 0xcd},	/* OTP set */
	{}
};

static const struct hda_verb cm9825_std_d0_verbs[] = {
	/* chip init verbs */
	{0x34, AC_VERB_SET_EAPD_BTLENABLE, 0x02},	/* EAPD set */
	{0x43, CM9825_VERB_SET_SNR, 0x30},	/* SNR set */
	{0x43, CM9825_VERB_SET_PLL, 0x00},	/* PLL set */
	{0x43, CM9825_VERB_SET_ADCL, 0x00},	/* ADC */
	{0x43, CM9825_VERB_SET_DACL, 0x02},	/* DACL */
	{0x43, CM9825_VERB_SET_MBIAS, 0x00},	/* MBIAS */
	{0x43, CM9825_VERB_SET_VNEG, 0x56},	/* VOL NEG */
	{0x43, CM9825_VERB_SET_D2S, 0x62},	/* depop */
	{0x43, CM9825_VERB_SET_DACTRL, 0x00},	/* DACTRL set */
	{0x43, CM9825_VERB_SET_PDNEG, 0x0c},	/* SEL OSC */
	{0x43, CM9825_VERB_SET_VDO, 0x80},	/* VDO set */
	{0x43, CM9825_VERB_SET_CDALR, 0xf4},	/* Class D */
	{0x43, CM9825_VERB_SET_OTP, 0xcd},	/* OTP set */
	{0x43, CM9825_VERB_SET_MTCBA, 0x61},	/* SR set */
	{0x43, CM9825_VERB_SET_OCP, 0x33},	/* OTP set */
	{0x43, CM9825_VERB_SET_GAD, 0x07},	/* ADC -3db */
	{0x43, CM9825_VERB_SET_TMOD, 0x26},	/* Class D clk */
	{0x3C, AC_VERB_SET_AMP_GAIN_MUTE |
		AC_AMP_SET_OUTPUT | AC_AMP_SET_RIGHT, 0x2d},	/* Gain set */
	{0x3C, AC_VERB_SET_AMP_GAIN_MUTE |
		AC_AMP_SET_OUTPUT | AC_AMP_SET_LEFT, 0x2d},	/* Gain set */
	{0x43, CM9825_VERB_SET_HPF_1, 0x40},	/* HPF set */
	{0x43, CM9825_VERB_SET_HPF_2, 0x40},	/* HPF set */
	{}
};

static const struct hda_verb cm9825_hp_present_verbs[] = {
	{0x42, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00},	/* PIN off */
	{0x43, CM9825_VERB_SET_ADCL, 0x88},	/* ADC */
	{0x43, CM9825_VERB_SET_DACL, 0xaa},	/* DACL */
	{0x43, CM9825_VERB_SET_MBIAS, 0x10},	/* MBIAS */
	{0x43, CM9825_VERB_SET_D2S, 0xf2},	/* depop */
	{0x43, CM9825_VERB_SET_DACTRL, 0x00},	/* DACTRL set */
	{0x43, CM9825_VERB_SET_VDO, 0xc4},	/* VDO set */
	{}
};

static const struct hda_verb cm9825_hp_remove_verbs[] = {
	{0x43, CM9825_VERB_SET_ADCL, 0x00},	/* ADC */
	{0x43, CM9825_VERB_SET_DACL, 0x56},	/* DACL */
	{0x43, CM9825_VERB_SET_MBIAS, 0x00},	/* MBIAS */
	{0x43, CM9825_VERB_SET_D2S, 0x62},	/* depop */
	{0x43, CM9825_VERB_SET_DACTRL, 0xe0},	/* DACTRL set */
	{0x43, CM9825_VERB_SET_VDO, 0x80},	/* VDO set */
	{0x42, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},	/* PIN on */
	{}
};

static void cm9825_unsol_hp_delayed(struct work_struct *work)
{
	struct cmi_spec *spec =
	    container_of(to_delayed_work(work), struct cmi_spec, unsol_hp_work);
	struct hda_jack_tbl *jack;
	hda_nid_t hp_pin = spec->gen.autocfg.hp_pins[0];
	bool hp_jack_plugin = false;
	int err = 0;

	hp_jack_plugin = snd_hda_jack_detect(spec->codec, hp_pin);

	codec_dbg(spec->codec, "hp_jack_plugin %d, hp_pin 0x%X\n",
		  (int)hp_jack_plugin, hp_pin);

	if (!hp_jack_plugin) {
		err =
		    snd_hda_codec_write(spec->codec, 0x42, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40);
		if (err)
			codec_dbg(spec->codec, "codec_write err %d\n", err);

		snd_hda_sequence_write(spec->codec, spec->chip_hp_remove_verbs);
	} else {
		snd_hda_sequence_write(spec->codec,
				       spec->chip_hp_present_verbs);
	}

	jack = snd_hda_jack_tbl_get(spec->codec, hp_pin);
	if (jack) {
		jack->block_report = 0;
		snd_hda_jack_report_sync(spec->codec);
	}
}

static void hp_callback(struct hda_codec *codec, struct hda_jack_callback *cb)
{
	struct cmi_spec *spec = codec->spec;
	struct hda_jack_tbl *tbl;

	/* Delay enabling the HP amp, to let the mic-detection
	 * state machine run.
	 */

	codec_dbg(spec->codec, "cb->nid 0x%X\n", cb->nid);

	tbl = snd_hda_jack_tbl_get(codec, cb->nid);
	if (tbl)
		tbl->block_report = 1;
	schedule_delayed_work(&spec->unsol_hp_work, msecs_to_jiffies(200));
}

static void cm9825_setup_unsol(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;

	hda_nid_t hp_pin = spec->gen.autocfg.hp_pins[0];

	snd_hda_jack_detect_enable_callback(codec, hp_pin, hp_callback);
}

static int cm9825_init(struct hda_codec *codec)
{
	snd_hda_gen_init(codec);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return 0;
}

static void cm9825_free(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;

	cancel_delayed_work_sync(&spec->unsol_hp_work);
	snd_hda_gen_free(codec);
}

static int cm9825_suspend(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;

	cancel_delayed_work_sync(&spec->unsol_hp_work);

	snd_hda_sequence_write(codec, spec->chip_d3_verbs);

	return 0;
}

static int cm9825_resume(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;
	hda_nid_t hp_pin = 0;
	bool hp_jack_plugin = false;
	int err;

	err =
	    snd_hda_codec_write(spec->codec, 0x42, 0,
				AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00);
	if (err)
		codec_dbg(codec, "codec_write err %d\n", err);

	msleep(150);		/* for depop noise */

	codec->patch_ops.init(codec);

	hp_pin = spec->gen.autocfg.hp_pins[0];
	hp_jack_plugin = snd_hda_jack_detect(spec->codec, hp_pin);

	codec_dbg(spec->codec, "hp_jack_plugin %d, hp_pin 0x%X\n",
		  (int)hp_jack_plugin, hp_pin);

	if (!hp_jack_plugin) {
		err =
		    snd_hda_codec_write(spec->codec, 0x42, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40);

		if (err)
			codec_dbg(codec, "codec_write err %d\n", err);

		snd_hda_sequence_write(codec, cm9825_hp_remove_verbs);
	}

	snd_hda_regmap_sync(codec);
	hda_call_check_power_status(codec, 0x01);

	return 0;
}

/*
 * stuff for auto-parser
 */
static const struct hda_codec_ops cmi_auto_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
};

static int patch_cm9825(struct hda_codec *codec)
{
	struct cmi_spec *spec;
	struct auto_pin_cfg *cfg;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	INIT_DELAYED_WORK(&spec->unsol_hp_work, cm9825_unsol_hp_delayed);
	codec->spec = spec;
	spec->codec = codec;
	codec->patch_ops = cmi_auto_patch_ops;
	codec->patch_ops.init = cm9825_init;
	codec->patch_ops.suspend = cm9825_suspend;
	codec->patch_ops.resume = cm9825_resume;
	codec->patch_ops.free = cm9825_free;
	codec->patch_ops.check_power_status = snd_hda_gen_check_power_status;
	cfg = &spec->gen.autocfg;
	snd_hda_gen_spec_init(&spec->gen);
	spec->chip_d0_verbs = cm9825_std_d0_verbs;
	spec->chip_d3_verbs = cm9825_std_d3_verbs;
	spec->chip_hp_present_verbs = cm9825_hp_present_verbs;
	spec->chip_hp_remove_verbs = cm9825_hp_remove_verbs;

	snd_hda_sequence_write(codec, spec->chip_d0_verbs);

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		goto error;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		goto error;

	cm9825_setup_unsol(codec);

	return 0;

 error:
	cm9825_free(codec);

	codec_info(codec, "Enter err %d\n", err);

	return err;
}

static int patch_cmi9880(struct hda_codec *codec)
{
	struct cmi_spec *spec;
	struct auto_pin_cfg *cfg;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	codec->patch_ops = cmi_auto_patch_ops;
	cfg = &spec->gen.autocfg;
	snd_hda_gen_spec_init(&spec->gen);

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		goto error;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		goto error;

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}

static int patch_cmi8888(struct hda_codec *codec)
{
	struct cmi_spec *spec;
	struct auto_pin_cfg *cfg;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;
	codec->patch_ops = cmi_auto_patch_ops;
	cfg = &spec->gen.autocfg;
	snd_hda_gen_spec_init(&spec->gen);

	/* mask NID 0x10 from the playback volume selection;
	 * it's a headphone boost volume handled manually below
	 */
	spec->gen.out_vol_mask = (1ULL << 0x10);

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		goto error;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		goto error;

	if (get_defcfg_device(snd_hda_codec_get_pincfg(codec, 0x10)) ==
	    AC_JACK_HP_OUT) {
		static const struct snd_kcontrol_new amp_kctl =
			HDA_CODEC_VOLUME("Headphone Amp Playback Volume",
					 0x10, 0, HDA_OUTPUT);
		if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &amp_kctl)) {
			err = -ENOMEM;
			goto error;
		}
	}

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}

/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_cmedia[] = {
	HDA_CODEC_ENTRY(0x13f68888, "CMI8888", patch_cmi8888),
	HDA_CODEC_ENTRY(0x13f69880, "CMI9880", patch_cmi9880),
	HDA_CODEC_ENTRY(0x434d4980, "CMI9880", patch_cmi9880),
	HDA_CODEC_ENTRY(0x13f69825, "CM9825", patch_cm9825),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_cmedia);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("C-Media HD-audio codec");

static struct hda_codec_driver cmedia_driver = {
	.id = snd_hda_id_cmedia,
};

module_hda_codec_driver(cmedia_driver);
