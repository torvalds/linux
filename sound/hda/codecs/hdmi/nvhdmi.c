// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nvidia HDMI codec support
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/tlv.h>
#include <sound/hdaudio.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hdmi_local.h"

enum {
	MODEL_GENERIC,
	MODEL_LEGACY,
};

/*
 * NVIDIA codecs ignore ASP mapping for 2ch - confirmed on:
 * - 0x10de0015
 * - 0x10de0040
 */
static int nvhdmi_chmap_cea_alloc_validate_get_type(struct hdac_chmap *chmap,
		struct hdac_cea_channel_speaker_allocation *cap, int channels)
{
	if (cap->ca_index == 0x00 && channels == 2)
		return SNDRV_CTL_TLVT_CHMAP_FIXED;

	/* If the speaker allocation matches the channel count, it is OK. */
	if (cap->channels != channels)
		return -1;

	/* all channels are remappable freely */
	return SNDRV_CTL_TLVT_CHMAP_VAR;
}

static int nvhdmi_chmap_validate(struct hdac_chmap *chmap,
		int ca, int chs, unsigned char *map)
{
	if (ca == 0x00 && (map[0] != SNDRV_CHMAP_FL || map[1] != SNDRV_CHMAP_FR))
		return -EINVAL;

	return 0;
}

/* map from pin NID to port; port is 0-based */
/* for Nvidia: assume widget NID starting from 4, with step 1 (4, 5, 6, ...) */
static int nvhdmi_pin2port(void *audio_ptr, int pin_nid)
{
	return pin_nid - 4;
}

/* reverse-map from port to pin NID: see above */
static int nvhdmi_port2pin(struct hda_codec *codec, int port)
{
	return port + 4;
}

static const struct drm_audio_component_audio_ops nvhdmi_audio_ops = {
	.pin2port = nvhdmi_pin2port,
	.pin_eld_notify = snd_hda_hdmi_acomp_pin_eld_notify,
	.master_bind = snd_hda_hdmi_acomp_master_bind,
	.master_unbind = snd_hda_hdmi_acomp_master_unbind,
};

static int probe_generic(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err;

	err = snd_hda_hdmi_generic_alloc(codec);
	if (err < 0)
		return err;
	codec->dp_mst = true;

	spec = codec->spec;

	err = snd_hda_hdmi_parse_codec(codec);
	if (err < 0) {
		snd_hda_hdmi_generic_spec_free(codec);
		return err;
	}

	snd_hda_hdmi_generic_init_per_pins(codec);

	spec->dyn_pin_out = true;

	spec->chmap.ops.chmap_cea_alloc_validate_get_type =
		nvhdmi_chmap_cea_alloc_validate_get_type;
	spec->chmap.ops.chmap_validate = nvhdmi_chmap_validate;
	spec->nv_dp_workaround = true;

	codec->link_down_at_suspend = 1;

	snd_hda_hdmi_acomp_init(codec, &nvhdmi_audio_ops, nvhdmi_port2pin);

	return 0;
}

static int probe_legacy(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err;

	err = snd_hda_hdmi_generic_probe(codec);
	if (err)
		return err;

	spec = codec->spec;
	spec->dyn_pin_out = true;

	spec->chmap.ops.chmap_cea_alloc_validate_get_type =
		nvhdmi_chmap_cea_alloc_validate_get_type;
	spec->chmap.ops.chmap_validate = nvhdmi_chmap_validate;
	spec->nv_dp_workaround = true;

	codec->link_down_at_suspend = 1;

	return 0;
}

static int nvhdmi_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	if (id->driver_data == MODEL_LEGACY)
		return probe_legacy(codec);
	else
		return probe_generic(codec);
}

static const struct hda_codec_ops nvhdmi_codec_ops = {
	.probe = nvhdmi_probe,
	.remove = snd_hda_hdmi_generic_remove,
	.init = snd_hda_hdmi_generic_init,
	.build_pcms = snd_hda_hdmi_generic_build_pcms,
	.build_controls = snd_hda_hdmi_generic_build_controls,
	.unsol_event = snd_hda_hdmi_generic_unsol_event,
	.suspend = snd_hda_hdmi_generic_suspend,
	.resume	 = snd_hda_hdmi_generic_resume,
};

static const struct hda_device_id snd_hda_id_nvhdmi[] = {
	HDA_CODEC_ID_MODEL(0x10de0008, "GPU 08 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0009, "GPU 09 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de000a, "GPU 0a HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de000b, "GPU 0b HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de000c, "MCP89 HDMI",		MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de000d, "GPU 0d HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0010, "GPU 10 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0011, "GPU 11 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0012, "GPU 12 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0013, "GPU 13 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0014, "GPU 14 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0015, "GPU 15 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0016, "GPU 16 HDMI/DP",	MODEL_LEGACY),
	/* 17 is known to be absent */
	HDA_CODEC_ID_MODEL(0x10de0018, "GPU 18 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0019, "GPU 19 HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de001a, "GPU 1a HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de001b, "GPU 1b HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de001c, "GPU 1c HDMI/DP",	MODEL_LEGACY),
	HDA_CODEC_ID_MODEL(0x10de0040, "GPU 40 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0041, "GPU 41 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0042, "GPU 42 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0043, "GPU 43 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0044, "GPU 44 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0045, "GPU 45 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0050, "GPU 50 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0051, "GPU 51 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0052, "GPU 52 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0060, "GPU 60 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0061, "GPU 61 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0062, "GPU 62 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0070, "GPU 70 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0071, "GPU 71 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0072, "GPU 72 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0073, "GPU 73 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0074, "GPU 74 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0076, "GPU 76 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de007b, "GPU 7b HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de007c, "GPU 7c HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de007d, "GPU 7d HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de007e, "GPU 7e HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0080, "GPU 80 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0081, "GPU 81 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0082, "GPU 82 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0083, "GPU 83 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0084, "GPU 84 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0090, "GPU 90 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0091, "GPU 91 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0092, "GPU 92 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0093, "GPU 93 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0094, "GPU 94 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0095, "GPU 95 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0097, "GPU 97 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0098, "GPU 98 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de0099, "GPU 99 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de009a, "GPU 9a HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de009b, "GPU 9b HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de009c, "GPU 9c HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de009d, "GPU 9d HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de009e, "GPU 9e HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de009f, "GPU 9f HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a0, "GPU a0 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a1, "GPU a1 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a3, "GPU a3 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a4, "GPU a4 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a5, "GPU a5 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a6, "GPU a6 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a7, "GPU a7 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a8, "GPU a8 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00a9, "GPU a9 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00aa, "GPU aa HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00ab, "GPU ab HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00ad, "GPU ad HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00ae, "GPU ae HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00af, "GPU af HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00b0, "GPU b0 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00b1, "GPU b1 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00c0, "GPU c0 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00c1, "GPU c1 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00c3, "GPU c3 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00c4, "GPU c4 HDMI/DP",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10de00c5, "GPU c5 HDMI/DP",	MODEL_GENERIC),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_nvhdmi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nvidia HDMI HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_HDMI");

static struct hda_codec_driver nvhdmi_driver = {
	.id = snd_hda_id_nvhdmi,
	.ops = &nvhdmi_codec_ops,
};

module_hda_codec_driver(nvhdmi_driver);
