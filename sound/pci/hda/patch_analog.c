/*
 * HD audio interface patch for AD1882, AD1884, AD1981HD, AD1983, AD1984,
 *   AD1986A, AD1988
 *
 * Copyright (c) 2005-2007 Takashi Iwai <tiwai@suse.de>
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
#include <linux/slab.h>
#include <linux/module.h>

#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "hda_generic.h"


struct ad198x_spec {
	struct hda_gen_spec gen;

	/* for auto parser */
	int smux_paths[4];
	unsigned int cur_smux;
	hda_nid_t eapd_nid;

	unsigned int beep_amp;	/* beep amp value, set via set_beep_amp() */
};


#ifdef CONFIG_SND_HDA_INPUT_BEEP
/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new ad_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_BEEP("Beep Playback Switch", 0, 0, HDA_OUTPUT),
	{ } /* end */
};

#define set_beep_amp(spec, nid, idx, dir) \
	((spec)->beep_amp = HDA_COMPOSE_AMP_VAL(nid, 1, idx, dir)) /* mono */
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#endif

#ifdef CONFIG_SND_HDA_INPUT_BEEP
static int create_beep_ctls(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	const struct snd_kcontrol_new *knew;

	if (!spec->beep_amp)
		return 0;

	for (knew = ad_beep_mixer ; knew->name; knew++) {
		int err;
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_new1(knew, codec);
		if (!kctl)
			return -ENOMEM;
		kctl->private_value = spec->beep_amp;
		err = snd_hda_ctl_add(codec, 0, kctl);
		if (err < 0)
			return err;
	}
	return 0;
}
#else
#define create_beep_ctls(codec)		0
#endif


static void ad198x_power_eapd_write(struct hda_codec *codec, hda_nid_t front,
				hda_nid_t hp)
{
	if (snd_hda_query_pin_caps(codec, front) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, front, 0, AC_VERB_SET_EAPD_BTLENABLE,
			    !codec->inv_eapd ? 0x00 : 0x02);
	if (snd_hda_query_pin_caps(codec, hp) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, hp, 0, AC_VERB_SET_EAPD_BTLENABLE,
			    !codec->inv_eapd ? 0x00 : 0x02);
}

static void ad198x_power_eapd(struct hda_codec *codec)
{
	/* We currently only handle front, HP */
	switch (codec->core.vendor_id) {
	case 0x11d41882:
	case 0x11d4882a:
	case 0x11d41884:
	case 0x11d41984:
	case 0x11d41883:
	case 0x11d4184a:
	case 0x11d4194a:
	case 0x11d4194b:
	case 0x11d41988:
	case 0x11d4198b:
	case 0x11d4989a:
	case 0x11d4989b:
		ad198x_power_eapd_write(codec, 0x12, 0x11);
		break;
	case 0x11d41981:
	case 0x11d41983:
		ad198x_power_eapd_write(codec, 0x05, 0x06);
		break;
	case 0x11d41986:
		ad198x_power_eapd_write(codec, 0x1b, 0x1a);
		break;
	}
}

static void ad198x_shutup(struct hda_codec *codec)
{
	snd_hda_shutup_pins(codec);
	ad198x_power_eapd(codec);
}

#ifdef CONFIG_PM
static int ad198x_suspend(struct hda_codec *codec)
{
	ad198x_shutup(codec);
	return 0;
}
#endif

/* follow EAPD via vmaster hook */
static void ad_vmaster_eapd_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct ad198x_spec *spec = codec->spec;

	if (!spec->eapd_nid)
		return;
	if (codec->inv_eapd)
		enabled = !enabled;
	snd_hda_codec_update_cache(codec, spec->eapd_nid, 0,
				   AC_VERB_SET_EAPD_BTLENABLE,
				   enabled ? 0x02 : 0x00);
}

/*
 * Automatic parse of I/O pins from the BIOS configuration
 */

static int ad198x_auto_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	err = create_beep_ctls(codec);
	if (err < 0)
		return err;
	return 0;
}

static const struct hda_codec_ops ad198x_auto_patch_ops = {
	.build_controls = ad198x_auto_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.check_power_status = snd_hda_gen_check_power_status,
	.suspend = ad198x_suspend,
#endif
	.reboot_notify = ad198x_shutup,
};


static int ad198x_parse_auto_config(struct hda_codec *codec, bool indep_hp)
{
	struct ad198x_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	int err;

	codec->spdif_status_reset = 1;
	codec->no_trigger_sense = 1;
	codec->no_sticky_stream = 1;

	spec->gen.indep_hp = indep_hp;
	if (!spec->gen.add_stereo_mix_input)
		spec->gen.add_stereo_mix_input = HDA_HINT_STEREO_MIX_AUTO;

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		return err;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		return err;

	return 0;
}

/*
 * AD1986A specific
 */

static int alloc_ad_spec(struct hda_codec *codec)
{
	struct ad198x_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	snd_hda_gen_spec_init(&spec->gen);
	codec->patch_ops = ad198x_auto_patch_ops;
	return 0;
}

/*
 * AD1986A fixup codes
 */

/* Lenovo N100 seems to report the reversed bit for HP jack-sensing */
static void ad_fixup_inv_jack_detect(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		codec->inv_jack_detect = 1;
		spec->gen.keep_eapd_on = 1;
		spec->gen.vmaster_mute.hook = ad_vmaster_eapd_hook;
		spec->eapd_nid = 0x1b;
	}
}

/* Toshiba Satellite L40 implements EAPD in a standard way unlike others */
static void ad1986a_fixup_eapd(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		codec->inv_eapd = 0;
		spec->gen.keep_eapd_on = 1;
		spec->eapd_nid = 0x1b;
	}
}

/* enable stereo-mix input for avoiding regression on KDE (bko#88251) */
static void ad1986a_fixup_eapd_mix_in(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		ad1986a_fixup_eapd(codec, fix, action);
		spec->gen.add_stereo_mix_input = HDA_HINT_STEREO_MIX_ENABLE;
	}
}

enum {
	AD1986A_FIXUP_INV_JACK_DETECT,
	AD1986A_FIXUP_ULTRA,
	AD1986A_FIXUP_SAMSUNG,
	AD1986A_FIXUP_3STACK,
	AD1986A_FIXUP_LAPTOP,
	AD1986A_FIXUP_LAPTOP_IMIC,
	AD1986A_FIXUP_EAPD,
	AD1986A_FIXUP_EAPD_MIX_IN,
	AD1986A_FIXUP_EASYNOTE,
};

static const struct hda_fixup ad1986a_fixups[] = {
	[AD1986A_FIXUP_INV_JACK_DETECT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad_fixup_inv_jack_detect,
	},
	[AD1986A_FIXUP_ULTRA] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x90170110 }, /* speaker */
			{ 0x1d, 0x90a7013e }, /* int mic */
			{}
		},
	},
	[AD1986A_FIXUP_SAMSUNG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x90170110 }, /* speaker */
			{ 0x1d, 0x90a7013e }, /* int mic */
			{ 0x20, 0x411111f0 }, /* N/A */
			{ 0x24, 0x411111f0 }, /* N/A */
			{}
		},
	},
	[AD1986A_FIXUP_3STACK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x02214021 }, /* headphone */
			{ 0x1b, 0x01014011 }, /* front */
			{ 0x1c, 0x01813030 }, /* line-in */
			{ 0x1d, 0x01a19020 }, /* rear mic */
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ 0x1f, 0x02a190f0 }, /* mic */
			{ 0x20, 0x411111f0 }, /* N/A */
			{}
		},
	},
	[AD1986A_FIXUP_LAPTOP] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x02214021 }, /* headphone */
			{ 0x1b, 0x90170110 }, /* speaker */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ 0x1f, 0x02a191f0 }, /* mic */
			{ 0x20, 0x411111f0 }, /* N/A */
			{}
		},
	},
	[AD1986A_FIXUP_LAPTOP_IMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1d, 0x90a7013e }, /* int mic */
			{}
		},
		.chained_before = 1,
		.chain_id = AD1986A_FIXUP_LAPTOP,
	},
	[AD1986A_FIXUP_EAPD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1986a_fixup_eapd,
	},
	[AD1986A_FIXUP_EAPD_MIX_IN] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1986a_fixup_eapd_mix_in,
	},
	[AD1986A_FIXUP_EASYNOTE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x0421402f }, /* headphone */
			{ 0x1b, 0x90170110 }, /* speaker */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x90a70130 }, /* int mic */
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ 0x1f, 0x04a19040 }, /* mic */
			{ 0x20, 0x411111f0 }, /* N/A */
			{ 0x21, 0x411111f0 }, /* N/A */
			{ 0x22, 0x411111f0 }, /* N/A */
			{ 0x23, 0x411111f0 }, /* N/A */
			{ 0x24, 0x411111f0 }, /* N/A */
			{ 0x25, 0x411111f0 }, /* N/A */
			{}
		},
		.chained = true,
		.chain_id = AD1986A_FIXUP_EAPD_MIX_IN,
	},
};

static const struct snd_pci_quirk ad1986a_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30af, "HP B2800", AD1986A_FIXUP_LAPTOP_IMIC),
	SND_PCI_QUIRK(0x1043, 0x1443, "ASUS Z99He", AD1986A_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1447, "ASUS A8JN", AD1986A_FIXUP_EAPD),
	SND_PCI_QUIRK_MASK(0x1043, 0xff00, 0x8100, "ASUS P5", AD1986A_FIXUP_3STACK),
	SND_PCI_QUIRK_MASK(0x1043, 0xff00, 0x8200, "ASUS M2", AD1986A_FIXUP_3STACK),
	SND_PCI_QUIRK(0x10de, 0xcb84, "ASUS A8N-VM", AD1986A_FIXUP_3STACK),
	SND_PCI_QUIRK(0x1179, 0xff40, "Toshiba Satellite L40", AD1986A_FIXUP_EAPD),
	SND_PCI_QUIRK(0x144d, 0xc01e, "FSC V2060", AD1986A_FIXUP_LAPTOP),
	SND_PCI_QUIRK_MASK(0x144d, 0xff00, 0xc000, "Samsung", AD1986A_FIXUP_SAMSUNG),
	SND_PCI_QUIRK(0x144d, 0xc027, "Samsung Q1", AD1986A_FIXUP_ULTRA),
	SND_PCI_QUIRK(0x1631, 0xc022, "PackardBell EasyNote MX65", AD1986A_FIXUP_EASYNOTE),
	SND_PCI_QUIRK(0x17aa, 0x2066, "Lenovo N100", AD1986A_FIXUP_INV_JACK_DETECT),
	SND_PCI_QUIRK(0x17aa, 0x1011, "Lenovo M55", AD1986A_FIXUP_3STACK),
	SND_PCI_QUIRK(0x17aa, 0x1017, "Lenovo A60", AD1986A_FIXUP_3STACK),
	{}
};

static const struct hda_model_fixup ad1986a_fixup_models[] = {
	{ .id = AD1986A_FIXUP_3STACK, .name = "3stack" },
	{ .id = AD1986A_FIXUP_LAPTOP, .name = "laptop" },
	{ .id = AD1986A_FIXUP_LAPTOP_IMIC, .name = "laptop-imic" },
	{ .id = AD1986A_FIXUP_LAPTOP_IMIC, .name = "laptop-eapd" }, /* alias */
	{ .id = AD1986A_FIXUP_EAPD, .name = "eapd" },
	{}
};

/*
 */
static int patch_ad1986a(struct hda_codec *codec)
{
	int err;
	struct ad198x_spec *spec;
	static hda_nid_t preferred_pairs[] = {
		0x1a, 0x03,
		0x1b, 0x03,
		0x1c, 0x04,
		0x1d, 0x05,
		0x1e, 0x03,
		0
	};

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	/* AD1986A has the inverted EAPD implementation */
	codec->inv_eapd = 1;

	spec->gen.mixer_nid = 0x07;
	spec->gen.beep_nid = 0x19;
	set_beep_amp(spec, 0x18, 0, HDA_OUTPUT);

	/* AD1986A has a hardware problem that it can't share a stream
	 * with multiple output pins.  The copy of front to surrounds
	 * causes noisy or silent outputs at a certain timing, e.g.
	 * changing the volume.
	 * So, let's disable the shared stream.
	 */
	spec->gen.multiout.no_share_stream = 1;
	/* give fixed DAC/pin pairs */
	spec->gen.preferred_dacs = preferred_pairs;

	/* AD1986A can't manage the dynamic pin on/off smoothly */
	spec->gen.auto_mute_via_amp = 1;

	snd_hda_pick_fixup(codec, ad1986a_fixup_models, ad1986a_fixup_tbl,
			   ad1986a_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec, false);
	if (err < 0) {
		snd_hda_gen_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}


/*
 * AD1983 specific
 */

/*
 * SPDIF mux control for AD1983 auto-parser
 */
static int ad1983_auto_smux_enum_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	static const char * const texts2[] = { "PCM", "ADC" };
	static const char * const texts3[] = { "PCM", "ADC1", "ADC2" };
	hda_nid_t dig_out = spec->gen.multiout.dig_out_nid;
	int num_conns = snd_hda_get_num_conns(codec, dig_out);

	if (num_conns == 2)
		return snd_hda_enum_helper_info(kcontrol, uinfo, 2, texts2);
	else if (num_conns == 3)
		return snd_hda_enum_helper_info(kcontrol, uinfo, 3, texts3);
	else
		return -EINVAL;
}

static int ad1983_auto_smux_enum_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_smux;
	return 0;
}

static int ad1983_auto_smux_enum_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int val = ucontrol->value.enumerated.item[0];
	hda_nid_t dig_out = spec->gen.multiout.dig_out_nid;
	int num_conns = snd_hda_get_num_conns(codec, dig_out);

	if (val >= num_conns)
		return -EINVAL;
	if (spec->cur_smux == val)
		return 0;
	spec->cur_smux = val;
	snd_hda_codec_write_cache(codec, dig_out, 0,
				  AC_VERB_SET_CONNECT_SEL, val);
	return 1;
}

static struct snd_kcontrol_new ad1983_auto_smux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Playback Source",
	.info = ad1983_auto_smux_enum_info,
	.get = ad1983_auto_smux_enum_get,
	.put = ad1983_auto_smux_enum_put,
};

static int ad1983_add_spdif_mux_ctl(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	hda_nid_t dig_out = spec->gen.multiout.dig_out_nid;
	int num_conns;

	if (!dig_out)
		return 0;
	num_conns = snd_hda_get_num_conns(codec, dig_out);
	if (num_conns != 2 && num_conns != 3)
		return 0;
	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &ad1983_auto_smux_mixer))
		return -ENOMEM;
	return 0;
}

static int patch_ad1983(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	static hda_nid_t conn_0c[] = { 0x08 };
	static hda_nid_t conn_0d[] = { 0x09 };
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x0e;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);

	/* limit the loopback routes not to confuse the parser */
	snd_hda_override_conn_list(codec, 0x0c, ARRAY_SIZE(conn_0c), conn_0c);
	snd_hda_override_conn_list(codec, 0x0d, ARRAY_SIZE(conn_0d), conn_0d);

	err = ad198x_parse_auto_config(codec, false);
	if (err < 0)
		goto error;
	err = ad1983_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;
	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * AD1981 HD specific
 */

static void ad1981_fixup_hp_eapd(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = ad_vmaster_eapd_hook;
		spec->eapd_nid = 0x05;
	}
}

/* set the upper-limit for mixer amp to 0dB for avoiding the possible
 * damage by overloading
 */
static void ad1981_fixup_amp_override(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_override_amp_caps(codec, 0x11, HDA_INPUT,
					  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (1 << AC_AMPCAP_MUTE_SHIFT));
}

enum {
	AD1981_FIXUP_AMP_OVERRIDE,
	AD1981_FIXUP_HP_EAPD,
};

static const struct hda_fixup ad1981_fixups[] = {
	[AD1981_FIXUP_AMP_OVERRIDE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1981_fixup_amp_override,
	},
	[AD1981_FIXUP_HP_EAPD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1981_fixup_hp_eapd,
		.chained = true,
		.chain_id = AD1981_FIXUP_AMP_OVERRIDE,
	},
};

static const struct snd_pci_quirk ad1981_fixup_tbl[] = {
	SND_PCI_QUIRK_VENDOR(0x1014, "Lenovo", AD1981_FIXUP_AMP_OVERRIDE),
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", AD1981_FIXUP_HP_EAPD),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Lenovo", AD1981_FIXUP_AMP_OVERRIDE),
	/* HP nx6320 (reversed SSID, H/W bug) */
	SND_PCI_QUIRK(0x30b0, 0x103c, "HP nx6320", AD1981_FIXUP_HP_EAPD),
	{}
};

static int patch_ad1981(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return -ENOMEM;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x0e;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x0d, 0, HDA_OUTPUT);

	snd_hda_pick_fixup(codec, NULL, ad1981_fixup_tbl, ad1981_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec, false);
	if (err < 0)
		goto error;
	err = ad1983_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * AD1988
 *
 * Output pins and routes
 *
 *        Pin               Mix     Sel     DAC (*)
 * port-A 0x11 (mute/hp) <- 0x22 <- 0x37 <- 03/04/06
 * port-B 0x14 (mute/hp) <- 0x2b <- 0x30 <- 03/04/06
 * port-C 0x15 (mute)    <- 0x2c <- 0x31 <- 05/0a
 * port-D 0x12 (mute/hp) <- 0x29         <- 04
 * port-E 0x17 (mute/hp) <- 0x26 <- 0x32 <- 05/0a
 * port-F 0x16 (mute)    <- 0x2a         <- 06
 * port-G 0x24 (mute)    <- 0x27         <- 05
 * port-H 0x25 (mute)    <- 0x28         <- 0a
 * mono   0x13 (mute/amp)<- 0x1e <- 0x36 <- 03/04/06
 *
 * DAC0 = 03h, DAC1 = 04h, DAC2 = 05h, DAC3 = 06h, DAC4 = 0ah
 * (*) DAC2/3/4 are swapped to DAC3/4/2 on AD198A rev.2 due to a h/w bug.
 *
 * Input pins and routes
 *
 *        pin     boost   mix input # / adc input #
 * port-A 0x11 -> 0x38 -> mix 2, ADC 0
 * port-B 0x14 -> 0x39 -> mix 0, ADC 1
 * port-C 0x15 -> 0x3a -> 33:0 - mix 1, ADC 2
 * port-D 0x12 -> 0x3d -> mix 3, ADC 8
 * port-E 0x17 -> 0x3c -> 34:0 - mix 4, ADC 4
 * port-F 0x16 -> 0x3b -> mix 5, ADC 3
 * port-G 0x24 -> N/A  -> 33:1 - mix 1, 34:1 - mix 4, ADC 6
 * port-H 0x25 -> N/A  -> 33:2 - mix 1, 34:2 - mix 4, ADC 7
 *
 *
 * DAC assignment
 *   6stack - front/surr/CLFE/side/opt DACs - 04/06/05/0a/03
 *   3stack - front/surr/CLFE/opt DACs - 04/05/0a/03
 *
 * Inputs of Analog Mix (0x20)
 *   0:Port-B (front mic)
 *   1:Port-C/G/H (line-in)
 *   2:Port-A
 *   3:Port-D (line-in/2)
 *   4:Port-E/G/H (mic-in)
 *   5:Port-F (mic2-in)
 *   6:CD
 *   7:Beep
 *
 * ADC selection
 *   0:Port-A
 *   1:Port-B (front mic-in)
 *   2:Port-C (line-in)
 *   3:Port-F (mic2-in)
 *   4:Port-E (mic-in)
 *   5:CD
 *   6:Port-G
 *   7:Port-H
 *   8:Port-D (line-in/2)
 *   9:Mix
 *
 * Proposed pin assignments by the datasheet
 *
 * 6-stack
 * Port-A front headphone
 *      B front mic-in
 *      C rear line-in
 *      D rear front-out
 *      E rear mic-in
 *      F rear surround
 *      G rear CLFE
 *      H rear side
 *
 * 3-stack
 * Port-A front headphone
 *      B front mic
 *      C rear line-in/surround
 *      D rear front-out
 *      E rear mic-in/CLFE
 *
 * laptop
 * Port-A headphone
 *      B mic-in
 *      C docking station
 *      D internal speaker (with EAPD)
 *      E/F quad mic array
 */

static int ad1988_auto_smux_enum_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	static const char * const texts[] = {
		"PCM", "ADC1", "ADC2", "ADC3",
	};
	int num_conns = snd_hda_get_num_conns(codec, 0x0b) + 1;
	if (num_conns > 4)
		num_conns = 4;
	return snd_hda_enum_helper_info(kcontrol, uinfo, num_conns, texts);
}

static int ad1988_auto_smux_enum_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_smux;
	return 0;
}

static int ad1988_auto_smux_enum_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int val = ucontrol->value.enumerated.item[0];
	struct nid_path *path;
	int num_conns = snd_hda_get_num_conns(codec, 0x0b) + 1;

	if (val >= num_conns)
		return -EINVAL;
	if (spec->cur_smux == val)
		return 0;

	mutex_lock(&codec->control_mutex);
	path = snd_hda_get_path_from_idx(codec,
					 spec->smux_paths[spec->cur_smux]);
	if (path)
		snd_hda_activate_path(codec, path, false, true);
	path = snd_hda_get_path_from_idx(codec, spec->smux_paths[val]);
	if (path)
		snd_hda_activate_path(codec, path, true, true);
	spec->cur_smux = val;
	mutex_unlock(&codec->control_mutex);
	return 1;
}

static struct snd_kcontrol_new ad1988_auto_smux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Playback Source",
	.info = ad1988_auto_smux_enum_info,
	.get = ad1988_auto_smux_enum_get,
	.put = ad1988_auto_smux_enum_put,
};

static int ad1988_auto_init(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i, err;

	err = snd_hda_gen_init(codec);
	if (err < 0)
		return err;
	if (!spec->gen.autocfg.dig_outs)
		return 0;

	for (i = 0; i < 4; i++) {
		struct nid_path *path;
		path = snd_hda_get_path_from_idx(codec, spec->smux_paths[i]);
		if (path)
			snd_hda_activate_path(codec, path, path->active, false);
	}

	return 0;
}

static int ad1988_add_spdif_mux_ctl(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i, num_conns;
	/* we create four static faked paths, since AD codecs have odd
	 * widget connections regarding the SPDIF out source
	 */
	static struct nid_path fake_paths[4] = {
		{
			.depth = 3,
			.path = { 0x02, 0x1d, 0x1b },
			.idx = { 0, 0, 0 },
			.multi = { 0, 0, 0 },
		},
		{
			.depth = 4,
			.path = { 0x08, 0x0b, 0x1d, 0x1b },
			.idx = { 0, 0, 1, 0 },
			.multi = { 0, 1, 0, 0 },
		},
		{
			.depth = 4,
			.path = { 0x09, 0x0b, 0x1d, 0x1b },
			.idx = { 0, 1, 1, 0 },
			.multi = { 0, 1, 0, 0 },
		},
		{
			.depth = 4,
			.path = { 0x0f, 0x0b, 0x1d, 0x1b },
			.idx = { 0, 2, 1, 0 },
			.multi = { 0, 1, 0, 0 },
		},
	};

	/* SPDIF source mux appears to be present only on AD1988A */
	if (!spec->gen.autocfg.dig_outs ||
	    get_wcaps_type(get_wcaps(codec, 0x1d)) != AC_WID_AUD_MIX)
		return 0;

	num_conns = snd_hda_get_num_conns(codec, 0x0b) + 1;
	if (num_conns != 3 && num_conns != 4)
		return 0;

	for (i = 0; i < num_conns; i++) {
		struct nid_path *path = snd_array_new(&spec->gen.paths);
		if (!path)
			return -ENOMEM;
		*path = fake_paths[i];
		if (!i)
			path->active = 1;
		spec->smux_paths[i] = snd_hda_get_path_idx(codec, path);
	}

	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &ad1988_auto_smux_mixer))
		return -ENOMEM;

	codec->patch_ops.init = ad1988_auto_init;

	return 0;
}

/*
 */

enum {
	AD1988_FIXUP_6STACK_DIG,
};

static const struct hda_fixup ad1988_fixups[] = {
	[AD1988_FIXUP_6STACK_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x11, 0x02214130 }, /* front-hp */
			{ 0x12, 0x01014010 }, /* line-out */
			{ 0x14, 0x02a19122 }, /* front-mic */
			{ 0x15, 0x01813021 }, /* line-in */
			{ 0x16, 0x01011012 }, /* line-out */
			{ 0x17, 0x01a19020 }, /* mic */
			{ 0x1b, 0x0145f1f0 }, /* SPDIF */
			{ 0x24, 0x01016011 }, /* line-out */
			{ 0x25, 0x01012013 }, /* line-out */
			{ }
		}
	},
};

static const struct hda_model_fixup ad1988_fixup_models[] = {
	{ .id = AD1988_FIXUP_6STACK_DIG, .name = "6stack-dig" },
	{}
};

static int patch_ad1988(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x20;
	spec->gen.mixer_merge_nid = 0x21;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);

	snd_hda_pick_fixup(codec, ad1988_fixup_models, NULL, ad1988_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec, true);
	if (err < 0)
		goto error;
	err = ad1988_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * AD1884 / AD1984
 *
 * port-B - front line/mic-in
 * port-E - aux in/out
 * port-F - aux in/out
 * port-C - rear line/mic-in
 * port-D - rear line/hp-out
 * port-A - front line/hp-out
 *
 * AD1984 = AD1884 + two digital mic-ins
 *
 * AD1883 / AD1884A / AD1984A / AD1984B
 *
 * port-B (0x14) - front mic-in
 * port-E (0x1c) - rear mic-in
 * port-F (0x16) - CD / ext out
 * port-C (0x15) - rear line-in
 * port-D (0x12) - rear line-out
 * port-A (0x11) - front hp-out
 *
 * AD1984A = AD1884A + digital-mic
 * AD1883 = equivalent with AD1984A
 * AD1984B = AD1984A + extra SPDIF-out
 */

/* set the upper-limit for mixer amp to 0dB for avoiding the possible
 * damage by overloading
 */
static void ad1884_fixup_amp_override(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_override_amp_caps(codec, 0x20, HDA_INPUT,
					  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (1 << AC_AMPCAP_MUTE_SHIFT));
}

/* toggle GPIO1 according to the mute state */
static void ad1884_vmaster_hp_gpio_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct ad198x_spec *spec = codec->spec;

	if (spec->eapd_nid)
		ad_vmaster_eapd_hook(private_data, enabled);
	snd_hda_codec_update_cache(codec, 0x01, 0,
				   AC_VERB_SET_GPIO_DATA,
				   enabled ? 0x00 : 0x02);
}

static void ad1884_fixup_hp_eapd(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gen.vmaster_mute.hook = ad1884_vmaster_hp_gpio_hook;
		spec->gen.own_eapd_ctl = 1;
		snd_hda_codec_write_cache(codec, 0x01, 0,
					  AC_VERB_SET_GPIO_MASK, 0x02);
		snd_hda_codec_write_cache(codec, 0x01, 0,
					  AC_VERB_SET_GPIO_DIRECTION, 0x02);
		snd_hda_codec_write_cache(codec, 0x01, 0,
					  AC_VERB_SET_GPIO_DATA, 0x02);
		break;
	case HDA_FIXUP_ACT_PROBE:
		if (spec->gen.autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT)
			spec->eapd_nid = spec->gen.autocfg.line_out_pins[0];
		else
			spec->eapd_nid = spec->gen.autocfg.speaker_pins[0];
		break;
	}
}

static void ad1884_fixup_thinkpad(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.keep_eapd_on = 1;
		spec->gen.vmaster_mute.hook = ad_vmaster_eapd_hook;
		spec->eapd_nid = 0x12;
		/* Analog PC Beeper - allow firmware/ACPI beeps */
		spec->beep_amp = HDA_COMPOSE_AMP_VAL(0x20, 3, 3, HDA_INPUT);
		spec->gen.beep_nid = 0; /* no digital beep */
	}
}

/* set magic COEFs for dmic */
static const struct hda_verb ad1884_dmic_init_verbs[] = {
	{0x01, AC_VERB_SET_COEF_INDEX, 0x13f7},
	{0x01, AC_VERB_SET_PROC_COEF, 0x08},
	{}
};

enum {
	AD1884_FIXUP_AMP_OVERRIDE,
	AD1884_FIXUP_HP_EAPD,
	AD1884_FIXUP_DMIC_COEF,
	AD1884_FIXUP_THINKPAD,
	AD1884_FIXUP_HP_TOUCHSMART,
};

static const struct hda_fixup ad1884_fixups[] = {
	[AD1884_FIXUP_AMP_OVERRIDE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1884_fixup_amp_override,
	},
	[AD1884_FIXUP_HP_EAPD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1884_fixup_hp_eapd,
		.chained = true,
		.chain_id = AD1884_FIXUP_AMP_OVERRIDE,
	},
	[AD1884_FIXUP_DMIC_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = ad1884_dmic_init_verbs,
	},
	[AD1884_FIXUP_THINKPAD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1884_fixup_thinkpad,
		.chained = true,
		.chain_id = AD1884_FIXUP_DMIC_COEF,
	},
	[AD1884_FIXUP_HP_TOUCHSMART] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = ad1884_dmic_init_verbs,
		.chained = true,
		.chain_id = AD1884_FIXUP_HP_EAPD,
	},
};

static const struct snd_pci_quirk ad1884_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x2a82, "HP Touchsmart", AD1884_FIXUP_HP_TOUCHSMART),
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", AD1884_FIXUP_HP_EAPD),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Lenovo Thinkpad", AD1884_FIXUP_THINKPAD),
	{}
};


static int patch_ad1884(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x20;
	spec->gen.mixer_merge_nid = 0x21;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);

	snd_hda_pick_fixup(codec, NULL, ad1884_fixup_tbl, ad1884_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec, true);
	if (err < 0)
		goto error;
	err = ad1983_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}

/*
 * AD1882 / AD1882A
 *
 * port-A - front hp-out
 * port-B - front mic-in
 * port-C - rear line-in, shared surr-out (3stack)
 * port-D - rear line-out
 * port-E - rear mic-in, shared clfe-out (3stack)
 * port-F - rear surr-out (6stack)
 * port-G - rear clfe-out (6stack)
 */

static int patch_ad1882(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x20;
	spec->gen.mixer_merge_nid = 0x21;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);
	err = ad198x_parse_auto_config(codec, true);
	if (err < 0)
		goto error;
	err = ad1988_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;
	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_analog[] = {
	{ .id = 0x11d4184a, .name = "AD1884A", .patch = patch_ad1884 },
	{ .id = 0x11d41882, .name = "AD1882", .patch = patch_ad1882 },
	{ .id = 0x11d41883, .name = "AD1883", .patch = patch_ad1884 },
	{ .id = 0x11d41884, .name = "AD1884", .patch = patch_ad1884 },
	{ .id = 0x11d4194a, .name = "AD1984A", .patch = patch_ad1884 },
	{ .id = 0x11d4194b, .name = "AD1984B", .patch = patch_ad1884 },
	{ .id = 0x11d41981, .name = "AD1981", .patch = patch_ad1981 },
	{ .id = 0x11d41983, .name = "AD1983", .patch = patch_ad1983 },
	{ .id = 0x11d41984, .name = "AD1984", .patch = patch_ad1884 },
	{ .id = 0x11d41986, .name = "AD1986A", .patch = patch_ad1986a },
	{ .id = 0x11d41988, .name = "AD1988", .patch = patch_ad1988 },
	{ .id = 0x11d4198b, .name = "AD1988B", .patch = patch_ad1988 },
	{ .id = 0x11d4882a, .name = "AD1882A", .patch = patch_ad1882 },
	{ .id = 0x11d4989a, .name = "AD1989A", .patch = patch_ad1988 },
	{ .id = 0x11d4989b, .name = "AD1989B", .patch = patch_ad1988 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:11d4*");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Analog Devices HD-audio codec");

static struct hda_codec_driver analog_driver = {
	.preset = snd_hda_preset_analog,
};

module_hda_codec_driver(analog_driver);
