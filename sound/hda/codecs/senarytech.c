// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD audio codec driver for Senary HDA audio codec
 *
 * Initially based on conexant.c
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>

#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "generic.h"

struct senary_spec {
	struct hda_gen_spec gen;

	/* extra EAPD pins */
	unsigned int num_eapds;
	hda_nid_t eapds[4];
	bool dynamic_eapd;
	hda_nid_t mute_led_eapd;

	unsigned int parse_flags; /* flag for snd_hda_parse_pin_defcfg() */

	int mute_led_polarity;
	unsigned int gpio_led;
	unsigned int gpio_mute_led_mask;
	unsigned int gpio_mic_led_mask;
};

enum {
	SENARY_FIXUP_PINCFG_DEFAULT,
};

static const struct hda_pintbl senary_pincfg_default[] = {
	{ 0x16, 0x02211020 }, /* Headphone */
	{ 0x17, 0x40f001f0 }, /* Not used */
	{ 0x18, 0x05a1904d }, /* Mic */
	{ 0x19, 0x02a1104e }, /* Headset Mic */
	{ 0x1a, 0x01819030 }, /* Line-in */
	{ 0x1d, 0x01014010 }, /* Line-out */
	{}
};

static const struct hda_fixup senary_fixups[] = {
	[SENARY_FIXUP_PINCFG_DEFAULT] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = senary_pincfg_default,
	},
};

/* Quirk table for specific machines can be added here */
static const struct hda_quirk sn6186_fixups[] = {
	{}
};

#ifdef CONFIG_SND_HDA_INPUT_BEEP
/* additional beep mixers; private_value will be overwritten */
static const struct snd_kcontrol_new senary_beep_mixer[] = {
	HDA_CODEC_VOLUME_MONO("Beep Playback Volume", 0, 1, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_BEEP_MONO("Beep Playback Switch", 0, 1, 0, HDA_OUTPUT),
};

static int set_beep_amp(struct senary_spec *spec, hda_nid_t nid,
			int idx, int dir)
{
	struct snd_kcontrol_new *knew;
	unsigned int beep_amp = HDA_COMPOSE_AMP_VAL(nid, 1, idx, dir);
	int i;

	for (i = 0; i < ARRAY_SIZE(senary_beep_mixer); i++) {
		knew = snd_hda_gen_add_kctl(&spec->gen, NULL,
					    &senary_beep_mixer[i]);
		if (!knew)
			return -ENOMEM;
		knew->private_value = beep_amp;
	}

	spec->gen.beep_nid = nid;
	return 0;
}

static int senary_auto_parse_beep(struct hda_codec *codec)
{
	struct senary_spec *spec = codec->spec;
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec)
		if ((get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_BEEP) &&
			(get_wcaps(codec, nid) & (AC_WCAP_OUT_AMP | AC_WCAP_AMP_OVRD)))
			return set_beep_amp(spec, nid, 0, HDA_OUTPUT);
	return 0;
}
#else
#define senary_auto_parse_beep(codec)	0
#endif

/* parse EAPDs */
static void senary_auto_parse_eapd(struct hda_codec *codec)
{
	struct senary_spec *spec = codec->spec;
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec) {
		if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
			continue;
		if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD))
			continue;
		spec->eapds[spec->num_eapds++] = nid;
		if (spec->num_eapds >= ARRAY_SIZE(spec->eapds))
			break;
	}
}

/* Hardware specific initialization verbs */
static void senary_init_verb(struct hda_codec *codec)
{
	/* Vendor specific init sequence */
	snd_hda_codec_write(codec, 0x1b, 0x0, 0x05a, 0xaa);
	snd_hda_codec_write(codec, 0x1b, 0x0, 0x059, 0x48);
	snd_hda_codec_write(codec, 0x1b, 0x0, 0x01b, 0x00);
	snd_hda_codec_write(codec, 0x1b, 0x0, 0x01c, 0x00);

	/* Override pin caps for headset mic */
	snd_hda_override_pin_caps(codec, 0x19, 0x2124);
}

static void senary_auto_turn_eapd(struct hda_codec *codec, int num_pins,
			      const hda_nid_t *pins, bool on)
{
	int i;

	for (i = 0; i < num_pins; i++) {
		snd_hda_codec_write(codec, pins[i], 0,
				    AC_VERB_SET_EAPD_BTLENABLE,
				    on ? 0x02 : 0);
	}
}

/* turn on/off EAPD according to Master switch */
static void senary_auto_vmaster_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct senary_spec *spec = codec->spec;

	senary_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, enabled);
}

static void senary_init_gpio_led(struct hda_codec *codec)
{
	struct senary_spec *spec = codec->spec;
	unsigned int mask = spec->gpio_mute_led_mask | spec->gpio_mic_led_mask;

	if (mask) {
		snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_MASK,
				    mask);
		snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_DIRECTION,
				    mask);
		snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_DATA,
				    spec->gpio_led);
	}
}

static int senary_init(struct hda_codec *codec)
{
	struct senary_spec *spec = codec->spec;

	snd_hda_gen_init(codec);
	senary_init_gpio_led(codec);
	senary_init_verb(codec);
	if (!spec->dynamic_eapd)
		senary_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, true);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return 0;
}

static void senary_shutdown(struct hda_codec *codec)
{
	struct senary_spec *spec = codec->spec;

	/* Turn the problematic codec into D3 to avoid spurious noises
	 * from the internal speaker during (and after) reboot
	 */
	senary_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, false);
}

static void senary_remove(struct hda_codec *codec)
{
	senary_shutdown(codec);
	snd_hda_gen_remove(codec);
}

static int senary_suspend(struct hda_codec *codec)
{
	senary_shutdown(codec);
	return 0;
}

static int senary_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct senary_spec *spec;
	int err;

	codec_info(codec, "%s: BIOS auto-probing.\n", codec->core.chip_name);

	spec = kzalloc_obj(*spec);
	if (!spec)
		return -ENOMEM;
	snd_hda_gen_spec_init(&spec->gen);
	codec->spec = spec;

	senary_auto_parse_eapd(codec);
	spec->gen.own_eapd_ctl = 1;

	/* Setup fixups based on codec vendor ID */
	switch (codec->core.vendor_id) {
	case 0x1fa86186:
		codec->pin_amp_workaround = 1;
		spec->gen.mixer_nid = 0x15;
		snd_hda_pick_fixup(codec, NULL, sn6186_fixups, senary_fixups);

		/* If no specific quirk found, apply the default pin configuration */
		if (codec->fixup_id == HDA_FIXUP_ID_NOT_SET)
			codec->fixup_id = SENARY_FIXUP_PINCFG_DEFAULT;
		break;
	default:
		snd_hda_pick_fixup(codec, NULL, sn6186_fixups, senary_fixups);
		break;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* Run hardware init verbs once during probe */
	senary_init_verb(codec);

	if (!spec->gen.vmaster_mute.hook)
		spec->gen.vmaster_mute.hook = senary_auto_vmaster_hook;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL,
				       spec->parse_flags);
	if (err < 0)
		goto error;

	err = senary_auto_parse_beep(codec);
	if (err < 0)
		goto error;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		goto error;

	/* Some laptops with Senary chips show stalls in S3 resume,
	 * which falls into the single-cmd mode.
	 * Better to make reset, then.
	 */
	if (!codec->bus->core.sync_write) {
		codec_info(codec,
			   "Enable sync_write for stable communication\n");
		codec->bus->core.sync_write = 1;
		codec->bus->allow_bus_reset = 1;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	senary_remove(codec);
	return err;
}

static const struct hda_codec_ops senary_codec_ops = {
	.probe = senary_probe,
	.remove = senary_remove,
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = senary_init,
	.unsol_event = snd_hda_jack_unsol_event,
	.suspend = senary_suspend,
	.check_power_status = snd_hda_gen_check_power_status,
	.stream_pm = snd_hda_gen_stream_pm,
};

/*
 */

static const struct hda_device_id snd_hda_id_senary[] = {
	HDA_CODEC_ID(0x1fa86186, "SN6186"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_senary);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Senarytech HD-audio codec");

static struct hda_codec_driver senary_driver = {
	.id = snd_hda_id_senary,
	.ops = &senary_codec_ops,
};

module_hda_codec_driver(senary_driver);
