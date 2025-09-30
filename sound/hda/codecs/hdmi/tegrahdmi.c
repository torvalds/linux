// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nvidia Tegra HDMI codec support
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
	MODEL_TEGRA,
	MODEL_TEGRA234,
};

/*
 * The HDA codec on NVIDIA Tegra contains two scratch registers that are
 * accessed using vendor-defined verbs. These registers can be used for
 * interoperability between the HDA and HDMI drivers.
 */

/* Audio Function Group node */
#define NVIDIA_AFG_NID 0x01

/*
 * The SCRATCH0 register is used to notify the HDMI codec of changes in audio
 * format. On Tegra, bit 31 is used as a trigger that causes an interrupt to
 * be raised in the HDMI codec. The remainder of the bits is arbitrary. This
 * implementation stores the HDA format (see AC_FMT_*) in bits [15:0] and an
 * additional bit (at position 30) to signal the validity of the format.
 *
 * | 31      | 30    | 29  16 | 15   0 |
 * +---------+-------+--------+--------+
 * | TRIGGER | VALID | UNUSED | FORMAT |
 * +-----------------------------------|
 *
 * Note that for the trigger bit to take effect it needs to change value
 * (i.e. it needs to be toggled). The trigger bit is not applicable from
 * TEGRA234 chip onwards, as new verb id 0xf80 will be used for interrupt
 * trigger to hdmi.
 */
#define NVIDIA_SET_HOST_INTR		0xf80
#define NVIDIA_GET_SCRATCH0		0xfa6
#define NVIDIA_SET_SCRATCH0_BYTE0	0xfa7
#define NVIDIA_SET_SCRATCH0_BYTE1	0xfa8
#define NVIDIA_SET_SCRATCH0_BYTE2	0xfa9
#define NVIDIA_SET_SCRATCH0_BYTE3	0xfaa
#define NVIDIA_SCRATCH_TRIGGER (1 << 7)
#define NVIDIA_SCRATCH_VALID   (1 << 6)

#define NVIDIA_GET_SCRATCH1		0xfab
#define NVIDIA_SET_SCRATCH1_BYTE0	0xfac
#define NVIDIA_SET_SCRATCH1_BYTE1	0xfad
#define NVIDIA_SET_SCRATCH1_BYTE2	0xfae
#define NVIDIA_SET_SCRATCH1_BYTE3	0xfaf

/*
 * The format parameter is the HDA audio format (see AC_FMT_*). If set to 0,
 * the format is invalidated so that the HDMI codec can be disabled.
 */
static void tegra_hdmi_set_format(struct hda_codec *codec,
				  hda_nid_t cvt_nid,
				  unsigned int format)
{
	unsigned int value;
	unsigned int nid = NVIDIA_AFG_NID;
	struct hdmi_spec *spec = codec->spec;

	/*
	 * Tegra HDA codec design from TEGRA234 chip onwards support DP MST.
	 * This resulted in moving scratch registers from audio function
	 * group to converter widget context. So CVT NID should be used for
	 * scratch register read/write for DP MST supported Tegra HDA codec.
	 */
	if (codec->dp_mst)
		nid = cvt_nid;

	/* bits [31:30] contain the trigger and valid bits */
	value = snd_hda_codec_read(codec, nid, 0,
				   NVIDIA_GET_SCRATCH0, 0);
	value = (value >> 24) & 0xff;

	/* bits [15:0] are used to store the HDA format */
	snd_hda_codec_write(codec, nid, 0,
			    NVIDIA_SET_SCRATCH0_BYTE0,
			    (format >> 0) & 0xff);
	snd_hda_codec_write(codec, nid, 0,
			    NVIDIA_SET_SCRATCH0_BYTE1,
			    (format >> 8) & 0xff);

	/* bits [16:24] are unused */
	snd_hda_codec_write(codec, nid, 0,
			    NVIDIA_SET_SCRATCH0_BYTE2, 0);

	/*
	 * Bit 30 signals that the data is valid and hence that HDMI audio can
	 * be enabled.
	 */
	if (format == 0)
		value &= ~NVIDIA_SCRATCH_VALID;
	else
		value |= NVIDIA_SCRATCH_VALID;

	if (spec->hdmi_intr_trig_ctrl) {
		/*
		 * For Tegra HDA Codec design from TEGRA234 onwards, the
		 * Interrupt to hdmi driver is triggered by writing
		 * non-zero values to verb 0xF80 instead of 31st bit of
		 * scratch register.
		 */
		snd_hda_codec_write(codec, nid, 0,
				NVIDIA_SET_SCRATCH0_BYTE3, value);
		snd_hda_codec_write(codec, nid, 0,
				NVIDIA_SET_HOST_INTR, 0x1);
	} else {
		/*
		 * Whenever the 31st trigger bit is toggled, an interrupt is raised
		 * in the HDMI codec. The HDMI driver will use that as trigger
		 * to update its configuration.
		 */
		value ^= NVIDIA_SCRATCH_TRIGGER;

		snd_hda_codec_write(codec, nid, 0,
				NVIDIA_SET_SCRATCH0_BYTE3, value);
	}
}

static int tegra_hdmi_pcm_prepare(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  unsigned int stream_tag,
				  unsigned int format,
				  struct snd_pcm_substream *substream)
{
	int err;

	err = snd_hda_hdmi_generic_pcm_prepare(hinfo, codec, stream_tag,
					       format, substream);
	if (err < 0)
		return err;

	/* notify the HDMI codec of the format change */
	tegra_hdmi_set_format(codec, hinfo->nid, format);

	return 0;
}

static int tegra_hdmi_pcm_cleanup(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream)
{
	/* invalidate the format in the HDMI codec */
	tegra_hdmi_set_format(codec, hinfo->nid, 0);

	return snd_hda_hdmi_generic_pcm_cleanup(hinfo, codec, substream);
}

static struct hda_pcm *hda_find_pcm_by_type(struct hda_codec *codec, int type)
{
	struct hdmi_spec *spec = codec->spec;
	unsigned int i;

	for (i = 0; i < spec->num_pins; i++) {
		struct hda_pcm *pcm = get_pcm_rec(spec, i);

		if (pcm->pcm_type == type)
			return pcm;
	}

	return NULL;
}

static int tegra_hdmi_build_pcms(struct hda_codec *codec)
{
	struct hda_pcm_stream *stream;
	struct hda_pcm *pcm;
	int err;

	err = snd_hda_hdmi_generic_build_pcms(codec);
	if (err < 0)
		return err;

	pcm = hda_find_pcm_by_type(codec, HDA_PCM_TYPE_HDMI);
	if (!pcm)
		return -ENODEV;

	/*
	 * Override ->prepare() and ->cleanup() operations to notify the HDMI
	 * codec about format changes.
	 */
	stream = &pcm->stream[SNDRV_PCM_STREAM_PLAYBACK];
	stream->ops.prepare = tegra_hdmi_pcm_prepare;
	stream->ops.cleanup = tegra_hdmi_pcm_cleanup;

	return 0;
}

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

static int tegra_hdmi_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int i, err;

	err = snd_hda_hdmi_parse_codec(codec);
	if (err < 0) {
		snd_hda_hdmi_generic_spec_free(codec);
		return err;
	}

	for (i = 0; i < spec->num_cvts; i++)
		snd_hda_codec_write(codec, spec->cvt_nids[i], 0,
					AC_VERB_SET_DIGI_CONVERT_1,
					AC_DIG1_ENABLE);

	snd_hda_hdmi_generic_init_per_pins(codec);

	codec->depop_delay = 10;
	spec->chmap.ops.chmap_cea_alloc_validate_get_type =
		nvhdmi_chmap_cea_alloc_validate_get_type;
	spec->chmap.ops.chmap_validate = nvhdmi_chmap_validate;

	spec->chmap.ops.chmap_cea_alloc_validate_get_type =
		nvhdmi_chmap_cea_alloc_validate_get_type;
	spec->chmap.ops.chmap_validate = nvhdmi_chmap_validate;
	spec->nv_dp_workaround = true;

	return 0;
}

static int tegrahdmi_probe(struct hda_codec *codec,
			   const struct hda_device_id *id)
{
	struct hdmi_spec *spec;
	int err;

	err = snd_hda_hdmi_generic_alloc(codec);
	if (err < 0)
		return err;

	if (id->driver_data == MODEL_TEGRA234) {
		codec->dp_mst = true;
		spec = codec->spec;
		spec->dyn_pin_out = true;
		spec->hdmi_intr_trig_ctrl = true;
	}

	return tegra_hdmi_init(codec);
}

static const struct hda_codec_ops tegrahdmi_codec_ops = {
	.probe = tegrahdmi_probe,
	.remove = snd_hda_hdmi_generic_remove,
	.init = snd_hda_hdmi_generic_init,
	.build_pcms = tegra_hdmi_build_pcms,
	.build_controls = snd_hda_hdmi_generic_build_controls,
	.unsol_event = snd_hda_hdmi_generic_unsol_event,
	.suspend = snd_hda_hdmi_generic_suspend,
	.resume = snd_hda_hdmi_generic_resume,
};

static const struct hda_device_id snd_hda_id_tegrahdmi[] = {
	HDA_CODEC_ID_MODEL(0x10de0020, "Tegra30 HDMI",		MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de0022, "Tegra114 HDMI",		MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de0028, "Tegra124 HDMI",		MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de0029, "Tegra210 HDMI/DP",	MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de002d, "Tegra186 HDMI/DP0",	MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de002e, "Tegra186 HDMI/DP1",	MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de002f, "Tegra194 HDMI/DP2",	MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de0030, "Tegra194 HDMI/DP3",	MODEL_TEGRA),
	HDA_CODEC_ID_MODEL(0x10de0031, "Tegra234 HDMI/DP",	MODEL_TEGRA234),
	HDA_CODEC_ID_MODEL(0x10de0033, "SoC 33 HDMI/DP",	MODEL_TEGRA234),
	HDA_CODEC_ID_MODEL(0x10de0034, "Tegra264 HDMI/DP",	MODEL_TEGRA234),
	HDA_CODEC_ID_MODEL(0x10de0035, "SoC 35 HDMI/DP",	MODEL_TEGRA234),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_tegrahdmi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nvidia Tegra HDMI HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_HDMI");

static struct hda_codec_driver tegrahdmi_driver = {
	.id = snd_hda_id_tegrahdmi,
	.ops = &tegrahdmi_codec_ops,
};

module_hda_codec_driver(tegrahdmi_driver);
