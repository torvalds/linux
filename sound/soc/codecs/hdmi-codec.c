// SPDX-License-Identifier: GPL-2.0-only
/*
 * ALSA SoC codec for HDMI encoder drivers
 * Copyright (C) 2015 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 */
#include <linux/module.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/pcm_drm_eld.h>
#include <sound/hdmi-codec.h>
#include <sound/pcm_iec958.h>

#include <drm/drm_crtc.h> /* This is only to get MAX_ELD_BYTES */

#define HDMI_CODEC_CHMAP_IDX_UNKNOWN  -1

struct hdmi_codec_channel_map_table {
	unsigned char map;	/* ALSA API channel map position */
};

/*
 * CEA speaker placement for HDMI 1.4:
 *
 *  FL  FLC   FC   FRC   FR   FRW
 *
 *                                  LFE
 *
 *  RL  RLC   RC   RRC   RR
 *
 *  Speaker placement has to be extended to support HDMI 2.0
 */
enum hdmi_codec_cea_spk_placement {
	FL  = BIT(0),	/* Front Left           */
	FC  = BIT(1),	/* Front Center         */
	FR  = BIT(2),	/* Front Right          */
	FLC = BIT(3),	/* Front Left Center    */
	FRC = BIT(4),	/* Front Right Center   */
	RL  = BIT(5),	/* Rear Left            */
	RC  = BIT(6),	/* Rear Center          */
	RR  = BIT(7),	/* Rear Right           */
	RLC = BIT(8),	/* Rear Left Center     */
	RRC = BIT(9),	/* Rear Right Center    */
	LFE = BIT(10),	/* Low Frequency Effect */
};

/*
 * cea Speaker allocation structure
 */
struct hdmi_codec_cea_spk_alloc {
	const int ca_id;
	unsigned int n_ch;
	unsigned long mask;
};

/* Channel maps  stereo HDMI */
static const struct snd_pcm_chmap_elem hdmi_codec_stereo_chmaps[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ }
};

/* Channel maps for multi-channel playbacks, up to 8 n_ch */
static const struct snd_pcm_chmap_elem hdmi_codec_8ch_chmaps[] = {
	{ .channels = 2, /* CA_ID 0x00 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 4, /* CA_ID 0x01 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA } },
	{ .channels = 4, /* CA_ID 0x02 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC } },
	{ .channels = 4, /* CA_ID 0x03 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC } },
	{ .channels = 6, /* CA_ID 0x04 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 6, /* CA_ID 0x05 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 6, /* CA_ID 0x06 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 6, /* CA_ID 0x07 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 6, /* CA_ID 0x08 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ .channels = 6, /* CA_ID 0x09 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ .channels = 6, /* CA_ID 0x0A */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ .channels = 6, /* CA_ID 0x0B */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ .channels = 8, /* CA_ID 0x0C */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 8, /* CA_ID 0x0D */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 8, /* CA_ID 0x0E */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 8, /* CA_ID 0x0F */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RC, SNDRV_CHMAP_NA } },
	{ .channels = 8, /* CA_ID 0x10 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RLC, SNDRV_CHMAP_RRC } },
	{ .channels = 8, /* CA_ID 0x11 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RLC, SNDRV_CHMAP_RRC } },
	{ .channels = 8, /* CA_ID 0x12 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RLC, SNDRV_CHMAP_RRC } },
	{ .channels = 8, /* CA_ID 0x13 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
		   SNDRV_CHMAP_RLC, SNDRV_CHMAP_RRC } },
	{ .channels = 8, /* CA_ID 0x14 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x15 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x16 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x17 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x18 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x19 */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x1A */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x1B */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x1C */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x1D */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x1E */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ .channels = 8, /* CA_ID 0x1F */
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_NA, SNDRV_CHMAP_NA,
		   SNDRV_CHMAP_FLC, SNDRV_CHMAP_FRC } },
	{ }
};

/*
 * hdmi_codec_channel_alloc: speaker configuration available for CEA
 *
 * This is an ordered list that must match with hdmi_codec_8ch_chmaps struct
 * The preceding ones have better chances to be selected by
 * hdmi_codec_get_ch_alloc_table_idx().
 */
static const struct hdmi_codec_cea_spk_alloc hdmi_codec_channel_alloc[] = {
	{ .ca_id = 0x00, .n_ch = 2,
	  .mask = FL | FR},
	/* 2.1 */
	{ .ca_id = 0x01, .n_ch = 4,
	  .mask = FL | FR | LFE},
	/* Dolby Surround */
	{ .ca_id = 0x02, .n_ch = 4,
	  .mask = FL | FR | FC },
	/* surround51 */
	{ .ca_id = 0x0b, .n_ch = 6,
	  .mask = FL | FR | LFE | FC | RL | RR},
	/* surround40 */
	{ .ca_id = 0x08, .n_ch = 6,
	  .mask = FL | FR | RL | RR },
	/* surround41 */
	{ .ca_id = 0x09, .n_ch = 6,
	  .mask = FL | FR | LFE | RL | RR },
	/* surround50 */
	{ .ca_id = 0x0a, .n_ch = 6,
	  .mask = FL | FR | FC | RL | RR },
	/* 6.1 */
	{ .ca_id = 0x0f, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | RC },
	/* surround71 */
	{ .ca_id = 0x13, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | RLC | RRC },
	/* others */
	{ .ca_id = 0x03, .n_ch = 8,
	  .mask = FL | FR | LFE | FC },
	{ .ca_id = 0x04, .n_ch = 8,
	  .mask = FL | FR | RC},
	{ .ca_id = 0x05, .n_ch = 8,
	  .mask = FL | FR | LFE | RC },
	{ .ca_id = 0x06, .n_ch = 8,
	  .mask = FL | FR | FC | RC },
	{ .ca_id = 0x07, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RC },
	{ .ca_id = 0x0c, .n_ch = 8,
	  .mask = FL | FR | RC | RL | RR },
	{ .ca_id = 0x0d, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | RC },
	{ .ca_id = 0x0e, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | RC },
	{ .ca_id = 0x10, .n_ch = 8,
	  .mask = FL | FR | RL | RR | RLC | RRC },
	{ .ca_id = 0x11, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | RLC | RRC },
	{ .ca_id = 0x12, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | RLC | RRC },
	{ .ca_id = 0x14, .n_ch = 8,
	  .mask = FL | FR | FLC | FRC },
	{ .ca_id = 0x15, .n_ch = 8,
	  .mask = FL | FR | LFE | FLC | FRC },
	{ .ca_id = 0x16, .n_ch = 8,
	  .mask = FL | FR | FC | FLC | FRC },
	{ .ca_id = 0x17, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | FLC | FRC },
	{ .ca_id = 0x18, .n_ch = 8,
	  .mask = FL | FR | RC | FLC | FRC },
	{ .ca_id = 0x19, .n_ch = 8,
	  .mask = FL | FR | LFE | RC | FLC | FRC },
	{ .ca_id = 0x1a, .n_ch = 8,
	  .mask = FL | FR | RC | FC | FLC | FRC },
	{ .ca_id = 0x1b, .n_ch = 8,
	  .mask = FL | FR | LFE | RC | FC | FLC | FRC },
	{ .ca_id = 0x1c, .n_ch = 8,
	  .mask = FL | FR | RL | RR | FLC | FRC },
	{ .ca_id = 0x1d, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | FLC | FRC },
	{ .ca_id = 0x1e, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | FLC | FRC },
	{ .ca_id = 0x1f, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | FLC | FRC },
};

struct hdmi_codec_priv {
	struct hdmi_codec_pdata hcd;
	uint8_t eld[MAX_ELD_BYTES];
	struct snd_pcm_chmap *chmap_info;
	unsigned int chmap_idx;
	struct mutex lock;
	bool busy;
	struct snd_soc_jack *jack;
	unsigned int jack_status;
	u8 iec_status[5];
};

static const struct snd_soc_dapm_widget hdmi_widgets[] = {
	SND_SOC_DAPM_OUTPUT("TX"),
	SND_SOC_DAPM_OUTPUT("RX"),
};

enum {
	DAI_ID_I2S = 0,
	DAI_ID_SPDIF,
};

static int hdmi_eld_ctl_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof_field(struct hdmi_codec_priv, eld);

	return 0;
}

static int hdmi_eld_ctl_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);

	memcpy(ucontrol->value.bytes.data, hcp->eld, sizeof(hcp->eld));

	return 0;
}

static unsigned long hdmi_codec_spk_mask_from_alloc(int spk_alloc)
{
	int i;
	static const unsigned long hdmi_codec_eld_spk_alloc_bits[] = {
		[0] = FL | FR, [1] = LFE, [2] = FC, [3] = RL | RR,
		[4] = RC, [5] = FLC | FRC, [6] = RLC | RRC,
	};
	unsigned long spk_mask = 0;

	for (i = 0; i < ARRAY_SIZE(hdmi_codec_eld_spk_alloc_bits); i++) {
		if (spk_alloc & (1 << i))
			spk_mask |= hdmi_codec_eld_spk_alloc_bits[i];
	}

	return spk_mask;
}

static void hdmi_codec_eld_chmap(struct hdmi_codec_priv *hcp)
{
	u8 spk_alloc;
	unsigned long spk_mask;

	spk_alloc = drm_eld_get_spk_alloc(hcp->eld);
	spk_mask = hdmi_codec_spk_mask_from_alloc(spk_alloc);

	/* Detect if only stereo supported, else return 8 channels mappings */
	if ((spk_mask & ~(FL | FR)) && hcp->chmap_info->max_channels > 2)
		hcp->chmap_info->chmap = hdmi_codec_8ch_chmaps;
	else
		hcp->chmap_info->chmap = hdmi_codec_stereo_chmaps;
}

static int hdmi_codec_get_ch_alloc_table_idx(struct hdmi_codec_priv *hcp,
					     unsigned char channels)
{
	int i;
	u8 spk_alloc;
	unsigned long spk_mask;
	const struct hdmi_codec_cea_spk_alloc *cap = hdmi_codec_channel_alloc;

	spk_alloc = drm_eld_get_spk_alloc(hcp->eld);
	spk_mask = hdmi_codec_spk_mask_from_alloc(spk_alloc);

	for (i = 0; i < ARRAY_SIZE(hdmi_codec_channel_alloc); i++, cap++) {
		/* If spk_alloc == 0, HDMI is unplugged return stereo config*/
		if (!spk_alloc && cap->ca_id == 0)
			return i;
		if (cap->n_ch != channels)
			continue;
		if (!(cap->mask == (spk_mask & cap->mask)))
			continue;
		return i;
	}

	return -EINVAL;
}
static int hdmi_codec_chmap_ctl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	unsigned const char *map;
	unsigned int i;
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hdmi_codec_priv *hcp = info->private_data;

	map = info->chmap[hcp->chmap_idx].map;

	for (i = 0; i < info->max_channels; i++) {
		if (hcp->chmap_idx == HDMI_CODEC_CHMAP_IDX_UNKNOWN)
			ucontrol->value.integer.value[i] = 0;
		else
			ucontrol->value.integer.value[i] = map[i];
	}

	return 0;
}

static int hdmi_codec_iec958_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int hdmi_codec_iec958_default_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);

	memcpy(ucontrol->value.iec958.status, hcp->iec_status,
	       sizeof(hcp->iec_status));

	return 0;
}

static int hdmi_codec_iec958_default_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);

	memcpy(hcp->iec_status, ucontrol->value.iec958.status,
	       sizeof(hcp->iec_status));

	return 0;
}

static int hdmi_codec_iec958_mask_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	memset(ucontrol->value.iec958.status, 0xff,
	       sizeof_field(struct hdmi_codec_priv, iec_status));

	return 0;
}

static int hdmi_codec_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ret = 0;

	mutex_lock(&hcp->lock);
	if (hcp->busy) {
		dev_err(dai->dev, "Only one simultaneous stream supported!\n");
		mutex_unlock(&hcp->lock);
		return -EINVAL;
	}

	if (hcp->hcd.ops->audio_startup) {
		ret = hcp->hcd.ops->audio_startup(dai->dev->parent, hcp->hcd.data);
		if (ret)
			goto err;
	}

	if (tx && hcp->hcd.ops->get_eld) {
		ret = hcp->hcd.ops->get_eld(dai->dev->parent, hcp->hcd.data,
					    hcp->eld, sizeof(hcp->eld));
		if (ret)
			goto err;

		ret = snd_pcm_hw_constraint_eld(substream->runtime, hcp->eld);
		if (ret)
			goto err;

		/* Select chmap supported */
		hdmi_codec_eld_chmap(hcp);
	}

	hcp->busy = true;

err:
	mutex_unlock(&hcp->lock);
	return ret;
}

static void hdmi_codec_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);

	hcp->chmap_idx = HDMI_CODEC_CHMAP_IDX_UNKNOWN;
	hcp->hcd.ops->audio_shutdown(dai->dev->parent, hcp->hcd.data);

	mutex_lock(&hcp->lock);
	hcp->busy = false;
	mutex_unlock(&hcp->lock);
}

static int hdmi_codec_fill_codec_params(struct snd_soc_dai *dai,
					unsigned int sample_width,
					unsigned int sample_rate,
					unsigned int channels,
					struct hdmi_codec_params *hp)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	int idx;

	/* Select a channel allocation that matches with ELD and pcm channels */
	idx = hdmi_codec_get_ch_alloc_table_idx(hcp, channels);
	if (idx < 0) {
		dev_err(dai->dev, "Not able to map channels to speakers (%d)\n",
			idx);
		hcp->chmap_idx = HDMI_CODEC_CHMAP_IDX_UNKNOWN;
		return idx;
	}

	memset(hp, 0, sizeof(*hp));

	hdmi_audio_infoframe_init(&hp->cea);
	hp->cea.channels = channels;
	hp->cea.coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	hp->cea.sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	hp->cea.sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;
	hp->cea.channel_allocation = hdmi_codec_channel_alloc[idx].ca_id;

	hp->sample_width = sample_width;
	hp->sample_rate = sample_rate;
	hp->channels = channels;

	hcp->chmap_idx = hdmi_codec_channel_alloc[idx].ca_id;

	return 0;
}

static int hdmi_codec_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	struct hdmi_codec_daifmt *cf = dai->playback_dma_data;
	struct hdmi_codec_params hp = {
		.iec = {
			.status = { 0 },
			.subcode = { 0 },
			.pad = 0,
			.dig_subframe = { 0 },
		}
	};
	int ret;

	if (!hcp->hcd.ops->hw_params)
		return 0;

	dev_dbg(dai->dev, "%s() width %d rate %d channels %d\n", __func__,
		params_width(params), params_rate(params),
		params_channels(params));

	ret = hdmi_codec_fill_codec_params(dai,
					   params_width(params),
					   params_rate(params),
					   params_channels(params),
					   &hp);
	if (ret < 0)
		return ret;

	memcpy(hp.iec.status, hcp->iec_status, sizeof(hp.iec.status));
	ret = snd_pcm_fill_iec958_consumer_hw_params(params, hp.iec.status,
						     sizeof(hp.iec.status));
	if (ret < 0) {
		dev_err(dai->dev, "Creating IEC958 channel status failed %d\n",
			ret);
		return ret;
	}

	cf->bit_fmt = params_format(params);
	return hcp->hcd.ops->hw_params(dai->dev->parent, hcp->hcd.data,
				       cf, &hp);
}

static int hdmi_codec_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	struct hdmi_codec_daifmt *cf = dai->playback_dma_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int channels = runtime->channels;
	unsigned int width = snd_pcm_format_width(runtime->format);
	unsigned int rate = runtime->rate;
	struct hdmi_codec_params hp;
	int ret;

	if (!hcp->hcd.ops->prepare)
		return 0;

	dev_dbg(dai->dev, "%s() width %d rate %d channels %d\n", __func__,
		width, rate, channels);

	ret = hdmi_codec_fill_codec_params(dai, width, rate, channels, &hp);
	if (ret < 0)
		return ret;

	memcpy(hp.iec.status, hcp->iec_status, sizeof(hp.iec.status));
	ret = snd_pcm_fill_iec958_consumer(runtime, hp.iec.status,
					   sizeof(hp.iec.status));
	if (ret < 0) {
		dev_err(dai->dev, "Creating IEC958 channel status failed %d\n",
			ret);
		return ret;
	}

	cf->bit_fmt = runtime->format;
	return hcp->hcd.ops->prepare(dai->dev->parent, hcp->hcd.data,
				     cf, &hp);
}

static int hdmi_codec_i2s_set_fmt(struct snd_soc_dai *dai,
				  unsigned int fmt)
{
	struct hdmi_codec_daifmt *cf = dai->playback_dma_data;

	/* Reset daifmt */
	memset(cf, 0, sizeof(*cf));

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		cf->bit_clk_master = 1;
		cf->frame_clk_master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		cf->frame_clk_master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		cf->bit_clk_master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		cf->frame_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		cf->bit_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		cf->frame_clk_inv = 1;
		cf->bit_clk_inv = 1;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		cf->fmt = HDMI_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		cf->fmt = HDMI_DSP_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		cf->fmt = HDMI_DSP_B;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		cf->fmt = HDMI_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		cf->fmt = HDMI_LEFT_J;
		break;
	case SND_SOC_DAIFMT_AC97:
		cf->fmt = HDMI_AC97;
		break;
	default:
		dev_err(dai->dev, "Invalid DAI interface format\n");
		return -EINVAL;
	}

	return 0;
}

static int hdmi_codec_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);

	/*
	 * ignore if direction was CAPTURE
	 * and it had .no_capture_mute flag
	 * see
	 *	snd_soc_dai_digital_mute()
	 */
	if (hcp->hcd.ops->mute_stream &&
	    (direction == SNDRV_PCM_STREAM_PLAYBACK ||
	     !hcp->hcd.ops->no_capture_mute))
		return hcp->hcd.ops->mute_stream(dai->dev->parent,
						 hcp->hcd.data,
						 mute, direction);

	return -ENOTSUPP;
}

/*
 * This driver can select all SND_SOC_DAIFMT_CBx_CFx,
 * but need to be selected from Sound Card, not be auto selected.
 * Because it might be used from other driver.
 * For example,
 *	${LINUX}/drivers/gpu/drm/bridge/synopsys/dw-hdmi-i2s-audio.c
 */
static u64 hdmi_codec_formats =
	SND_SOC_POSSIBLE_DAIFMT_NB_NF	|
	SND_SOC_POSSIBLE_DAIFMT_NB_IF	|
	SND_SOC_POSSIBLE_DAIFMT_IB_NF	|
	SND_SOC_POSSIBLE_DAIFMT_IB_IF	|
	SND_SOC_POSSIBLE_DAIFMT_I2S	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_A	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_B	|
	SND_SOC_POSSIBLE_DAIFMT_RIGHT_J	|
	SND_SOC_POSSIBLE_DAIFMT_LEFT_J	|
	SND_SOC_POSSIBLE_DAIFMT_AC97;

static const struct snd_soc_dai_ops hdmi_codec_i2s_dai_ops = {
	.startup	= hdmi_codec_startup,
	.shutdown	= hdmi_codec_shutdown,
	.hw_params	= hdmi_codec_hw_params,
	.prepare	= hdmi_codec_prepare,
	.set_fmt	= hdmi_codec_i2s_set_fmt,
	.mute_stream	= hdmi_codec_mute,
	.auto_selectable_formats	= &hdmi_codec_formats,
	.num_auto_selectable_formats	= 1,
};

static const struct snd_soc_dai_ops hdmi_codec_spdif_dai_ops = {
	.startup	= hdmi_codec_startup,
	.shutdown	= hdmi_codec_shutdown,
	.hw_params	= hdmi_codec_hw_params,
	.mute_stream	= hdmi_codec_mute,
};

#define HDMI_RATES	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
			 SNDRV_PCM_RATE_192000)

#define SPDIF_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE |\
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE)

/*
 * This list is only for formats allowed on the I2S bus. So there is
 * some formats listed that are not supported by HDMI interface. For
 * instance allowing the 32-bit formats enables 24-precision with CPU
 * DAIs that do not support 24-bit formats. If the extra formats cause
 * problems, we should add the video side driver an option to disable
 * them.
 */
#define I2S_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE |\
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |\
			 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |\
			 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)

static struct snd_kcontrol_new hdmi_codec_controls[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, MASK),
		.info = hdmi_codec_iec958_info,
		.get = hdmi_codec_iec958_mask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = hdmi_codec_iec958_info,
		.get = hdmi_codec_iec958_default_get,
		.put = hdmi_codec_iec958_default_put,
	},
	{
		.access	= (SNDRV_CTL_ELEM_ACCESS_READ |
			   SNDRV_CTL_ELEM_ACCESS_VOLATILE),
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= "ELD",
		.info	= hdmi_eld_ctl_info,
		.get	= hdmi_eld_ctl_get,
	},
};

static int hdmi_codec_pcm_new(struct snd_soc_pcm_runtime *rtd,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_dai_driver *drv = dai->driver;
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	unsigned int i;
	int ret;

	ret =  snd_pcm_add_chmap_ctls(rtd->pcm, SNDRV_PCM_STREAM_PLAYBACK,
				      NULL, drv->playback.channels_max, 0,
				      &hcp->chmap_info);
	if (ret < 0)
		return ret;

	/* override handlers */
	hcp->chmap_info->private_data = hcp;
	hcp->chmap_info->kctl->get = hdmi_codec_chmap_ctl_get;

	/* default chmap supported is stereo */
	hcp->chmap_info->chmap = hdmi_codec_stereo_chmaps;
	hcp->chmap_idx = HDMI_CODEC_CHMAP_IDX_UNKNOWN;

	for (i = 0; i < ARRAY_SIZE(hdmi_codec_controls); i++) {
		struct snd_kcontrol *kctl;

		/* add ELD ctl with the device number corresponding to the PCM stream */
		kctl = snd_ctl_new1(&hdmi_codec_controls[i], dai->component);
		if (!kctl)
			return -ENOMEM;

		kctl->id.device = rtd->pcm->device;
		ret = snd_ctl_add(rtd->card->snd_card, kctl);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int hdmi_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_context *dapm;
	struct hdmi_codec_daifmt *daifmt;
	struct snd_soc_dapm_route route[] = {
		{
			.sink = "TX",
			.source = dai->driver->playback.stream_name,
		},
		{
			.sink = dai->driver->capture.stream_name,
			.source = "RX",
		},
	};
	int ret;

	dapm = snd_soc_component_get_dapm(dai->component);
	ret = snd_soc_dapm_add_routes(dapm, route, 2);
	if (ret)
		return ret;

	daifmt = kzalloc(sizeof(*daifmt), GFP_KERNEL);
	if (!daifmt)
		return -ENOMEM;

	dai->playback_dma_data = daifmt;
	return 0;
}

static void hdmi_codec_jack_report(struct hdmi_codec_priv *hcp,
				   unsigned int jack_status)
{
	if (hcp->jack && jack_status != hcp->jack_status) {
		snd_soc_jack_report(hcp->jack, jack_status, SND_JACK_LINEOUT);
		hcp->jack_status = jack_status;
	}
}

static void plugged_cb(struct device *dev, bool plugged)
{
	struct hdmi_codec_priv *hcp = dev_get_drvdata(dev);

	if (plugged) {
		if (hcp->hcd.ops->get_eld) {
			hcp->hcd.ops->get_eld(dev->parent, hcp->hcd.data,
					    hcp->eld, sizeof(hcp->eld));
		}
		hdmi_codec_jack_report(hcp, SND_JACK_LINEOUT);
	} else {
		hdmi_codec_jack_report(hcp, 0);
		memset(hcp->eld, 0, sizeof(hcp->eld));
	}
}

static int hdmi_codec_set_jack(struct snd_soc_component *component,
			       struct snd_soc_jack *jack,
			       void *data)
{
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);
	int ret = -ENOTSUPP;

	if (hcp->hcd.ops->hook_plugged_cb) {
		hcp->jack = jack;
		ret = hcp->hcd.ops->hook_plugged_cb(component->dev->parent,
						    hcp->hcd.data,
						    plugged_cb,
						    component->dev);
		if (ret)
			hcp->jack = NULL;
	}
	return ret;
}

static int hdmi_dai_spdif_probe(struct snd_soc_dai *dai)
{
	struct hdmi_codec_daifmt *cf;
	int ret;

	ret = hdmi_dai_probe(dai);
	if (ret)
		return ret;

	cf = dai->playback_dma_data;
	cf->fmt = HDMI_SPDIF;

	return 0;
}

static int hdmi_codec_dai_remove(struct snd_soc_dai *dai)
{
	kfree(dai->playback_dma_data);
	return 0;
}

static const struct snd_soc_dai_driver hdmi_i2s_dai = {
	.name = "i2s-hifi",
	.id = DAI_ID_I2S,
	.probe = hdmi_dai_probe,
	.remove = hdmi_codec_dai_remove,
	.playback = {
		.stream_name = "I2S Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = HDMI_RATES,
		.formats = I2S_FORMATS,
		.sig_bits = 24,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = HDMI_RATES,
		.formats = I2S_FORMATS,
		.sig_bits = 24,
	},
	.ops = &hdmi_codec_i2s_dai_ops,
	.pcm_new = hdmi_codec_pcm_new,
};

static const struct snd_soc_dai_driver hdmi_spdif_dai = {
	.name = "spdif-hifi",
	.id = DAI_ID_SPDIF,
	.probe = hdmi_dai_spdif_probe,
	.remove = hdmi_codec_dai_remove,
	.playback = {
		.stream_name = "SPDIF Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = HDMI_RATES,
		.formats = SPDIF_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = HDMI_RATES,
		.formats = SPDIF_FORMATS,
	},
	.ops = &hdmi_codec_spdif_dai_ops,
	.pcm_new = hdmi_codec_pcm_new,
};

static int hdmi_of_xlate_dai_id(struct snd_soc_component *component,
				 struct device_node *endpoint)
{
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);
	int ret = -ENOTSUPP; /* see snd_soc_get_dai_id() */

	if (hcp->hcd.ops->get_dai_id)
		ret = hcp->hcd.ops->get_dai_id(component, endpoint);

	return ret;
}

static void hdmi_remove(struct snd_soc_component *component)
{
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);

	if (hcp->hcd.ops->hook_plugged_cb)
		hcp->hcd.ops->hook_plugged_cb(component->dev->parent,
					      hcp->hcd.data, NULL, NULL);
}

static const struct snd_soc_component_driver hdmi_driver = {
	.remove			= hdmi_remove,
	.dapm_widgets		= hdmi_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(hdmi_widgets),
	.of_xlate_dai_id	= hdmi_of_xlate_dai_id,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
	.set_jack		= hdmi_codec_set_jack,
};

static int hdmi_codec_probe(struct platform_device *pdev)
{
	struct hdmi_codec_pdata *hcd = pdev->dev.platform_data;
	struct snd_soc_dai_driver *daidrv;
	struct device *dev = &pdev->dev;
	struct hdmi_codec_priv *hcp;
	int dai_count, i = 0;
	int ret;

	if (!hcd) {
		dev_err(dev, "%s: No platform data\n", __func__);
		return -EINVAL;
	}

	dai_count = hcd->i2s + hcd->spdif;
	if (dai_count < 1 || !hcd->ops ||
	    (!hcd->ops->hw_params && !hcd->ops->prepare) ||
	    !hcd->ops->audio_shutdown) {
		dev_err(dev, "%s: Invalid parameters\n", __func__);
		return -EINVAL;
	}

	hcp = devm_kzalloc(dev, sizeof(*hcp), GFP_KERNEL);
	if (!hcp)
		return -ENOMEM;

	hcp->hcd = *hcd;
	mutex_init(&hcp->lock);

	ret = snd_pcm_create_iec958_consumer_default(hcp->iec_status,
						     sizeof(hcp->iec_status));
	if (ret < 0)
		return ret;

	daidrv = devm_kcalloc(dev, dai_count, sizeof(*daidrv), GFP_KERNEL);
	if (!daidrv)
		return -ENOMEM;

	if (hcd->i2s) {
		daidrv[i] = hdmi_i2s_dai;
		daidrv[i].playback.channels_max = hcd->max_i2s_channels;
		i++;
	}

	if (hcd->spdif)
		daidrv[i] = hdmi_spdif_dai;

	dev_set_drvdata(dev, hcp);

	ret = devm_snd_soc_register_component(dev, &hdmi_driver, daidrv,
					      dai_count);
	if (ret) {
		dev_err(dev, "%s: snd_soc_register_component() failed (%d)\n",
			__func__, ret);
		return ret;
	}
	return 0;
}

static struct platform_driver hdmi_codec_driver = {
	.driver = {
		.name = HDMI_CODEC_DRV_NAME,
	},
	.probe = hdmi_codec_probe,
};

module_platform_driver(hdmi_codec_driver);

MODULE_AUTHOR("Jyri Sarha <jsarha@ti.com>");
MODULE_DESCRIPTION("HDMI Audio Codec Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" HDMI_CODEC_DRV_NAME);
