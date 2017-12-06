/*
 * ALSA SoC codec for HDMI encoder drivers
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <sound/core.h>
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
	unsigned long spk_mask;		/* speaker position bit mask */
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
	struct snd_soc_dai_driver *daidrv;
	struct hdmi_codec_daifmt daifmt[2];
	struct mutex current_stream_lock;
	struct snd_pcm_substream *current_stream;
	uint8_t eld[MAX_ELD_BYTES];
	struct snd_pcm_chmap *chmap_info;
	unsigned int chmap_idx;
};

static const struct snd_soc_dapm_widget hdmi_widgets[] = {
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route hdmi_routes[] = {
	{ "TX", NULL, "Playback" },
};

enum {
	DAI_ID_I2S = 0,
	DAI_ID_SPDIF,
};

static int hdmi_eld_ctl_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = FIELD_SIZEOF(struct hdmi_codec_priv, eld);

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

static int hdmi_codec_new_stream(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	mutex_lock(&hcp->current_stream_lock);
	if (!hcp->current_stream) {
		hcp->current_stream = substream;
	} else if (hcp->current_stream != substream) {
		dev_err(dai->dev, "Only one simultaneous stream supported!\n");
		ret = -EINVAL;
	}
	mutex_unlock(&hcp->current_stream_lock);

	return ret;
}

static int hdmi_codec_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	dev_dbg(dai->dev, "%s()\n", __func__);

	ret = hdmi_codec_new_stream(substream, dai);
	if (ret)
		return ret;

	if (hcp->hcd.ops->audio_startup) {
		ret = hcp->hcd.ops->audio_startup(dai->dev->parent, hcp->hcd.data);
		if (ret) {
			mutex_lock(&hcp->current_stream_lock);
			hcp->current_stream = NULL;
			mutex_unlock(&hcp->current_stream_lock);
			return ret;
		}
	}

	if (hcp->hcd.ops->get_eld) {
		ret = hcp->hcd.ops->get_eld(dai->dev->parent, hcp->hcd.data,
					    hcp->eld, sizeof(hcp->eld));

		if (!ret) {
			ret = snd_pcm_hw_constraint_eld(substream->runtime,
							hcp->eld);
			if (ret)
				return ret;
		}
		/* Select chmap supported */
		hdmi_codec_eld_chmap(hcp);
	}
	return 0;
}

static void hdmi_codec_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dai->dev, "%s()\n", __func__);

	WARN_ON(hcp->current_stream != substream);

	hcp->chmap_idx = HDMI_CODEC_CHMAP_IDX_UNKNOWN;
	hcp->hcd.ops->audio_shutdown(dai->dev->parent, hcp->hcd.data);

	mutex_lock(&hcp->current_stream_lock);
	hcp->current_stream = NULL;
	mutex_unlock(&hcp->current_stream_lock);
}

static int hdmi_codec_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	struct hdmi_codec_params hp = {
		.iec = {
			.status = { 0 },
			.subcode = { 0 },
			.pad = 0,
			.dig_subframe = { 0 },
		}
	};
	int ret, idx;

	dev_dbg(dai->dev, "%s() width %d rate %d channels %d\n", __func__,
		params_width(params), params_rate(params),
		params_channels(params));

	if (params_width(params) > 24)
		params->msbits = 24;

	ret = snd_pcm_create_iec958_consumer_hw_params(params, hp.iec.status,
						       sizeof(hp.iec.status));
	if (ret < 0) {
		dev_err(dai->dev, "Creating IEC958 channel status failed %d\n",
			ret);
		return ret;
	}

	ret = hdmi_codec_new_stream(substream, dai);
	if (ret)
		return ret;

	hdmi_audio_infoframe_init(&hp.cea);
	hp.cea.channels = params_channels(params);
	hp.cea.coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	hp.cea.sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	hp.cea.sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;

	/* Select a channel allocation that matches with ELD and pcm channels */
	idx = hdmi_codec_get_ch_alloc_table_idx(hcp, hp.cea.channels);
	if (idx < 0) {
		dev_err(dai->dev, "Not able to map channels to speakers (%d)\n",
			idx);
		hcp->chmap_idx = HDMI_CODEC_CHMAP_IDX_UNKNOWN;
		return idx;
	}
	hp.cea.channel_allocation = hdmi_codec_channel_alloc[idx].ca_id;
	hcp->chmap_idx = hdmi_codec_channel_alloc[idx].ca_id;

	hp.sample_width = params_width(params);
	hp.sample_rate = params_rate(params);
	hp.channels = params_channels(params);

	return hcp->hcd.ops->hw_params(dai->dev->parent, hcp->hcd.data,
				       &hcp->daifmt[dai->id], &hp);
}

static int hdmi_codec_set_fmt(struct snd_soc_dai *dai,
			      unsigned int fmt)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	struct hdmi_codec_daifmt cf = { 0 };
	int ret = 0;

	dev_dbg(dai->dev, "%s()\n", __func__);

	if (dai->id == DAI_ID_SPDIF) {
		cf.fmt = HDMI_SPDIF;
	} else {
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFM:
			cf.bit_clk_master = 1;
			cf.frame_clk_master = 1;
			break;
		case SND_SOC_DAIFMT_CBS_CFM:
			cf.frame_clk_master = 1;
			break;
		case SND_SOC_DAIFMT_CBM_CFS:
			cf.bit_clk_master = 1;
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
			cf.frame_clk_inv = 1;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			cf.bit_clk_inv = 1;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			cf.frame_clk_inv = 1;
			cf.bit_clk_inv = 1;
			break;
		}

		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			cf.fmt = HDMI_I2S;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			cf.fmt = HDMI_DSP_A;
			break;
		case SND_SOC_DAIFMT_DSP_B:
			cf.fmt = HDMI_DSP_B;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			cf.fmt = HDMI_RIGHT_J;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			cf.fmt = HDMI_LEFT_J;
			break;
		case SND_SOC_DAIFMT_AC97:
			cf.fmt = HDMI_AC97;
			break;
		default:
			dev_err(dai->dev, "Invalid DAI interface format\n");
			return -EINVAL;
		}
	}

	hcp->daifmt[dai->id] = cf;

	return ret;
}

static int hdmi_codec_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dai->dev, "%s()\n", __func__);

	if (hcp->hcd.ops->digital_mute)
		return hcp->hcd.ops->digital_mute(dai->dev->parent,
						  hcp->hcd.data, mute);

	return 0;
}

static const struct snd_soc_dai_ops hdmi_dai_ops = {
	.startup	= hdmi_codec_startup,
	.shutdown	= hdmi_codec_shutdown,
	.hw_params	= hdmi_codec_hw_params,
	.set_fmt	= hdmi_codec_set_fmt,
	.digital_mute	= hdmi_codec_digital_mute,
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
			 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE)

static int hdmi_codec_pcm_new(struct snd_soc_pcm_runtime *rtd,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_dai_driver *drv = dai->driver;
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_new hdmi_eld_ctl = {
		.access	= SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= "ELD",
		.info	= hdmi_eld_ctl_info,
		.get	= hdmi_eld_ctl_get,
		.device	= rtd->pcm->device,
	};
	int ret;

	dev_dbg(dai->dev, "%s()\n", __func__);

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

	/* add ELD ctl with the device number corresponding to the PCM stream */
	kctl = snd_ctl_new1(&hdmi_eld_ctl, dai->component);
	if (!kctl)
		return -ENOMEM;

	return snd_ctl_add(rtd->card->snd_card, kctl);
}

static const struct snd_soc_dai_driver hdmi_i2s_dai = {
	.name = "i2s-hifi",
	.id = DAI_ID_I2S,
	.playback = {
		.stream_name = "I2S Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = HDMI_RATES,
		.formats = I2S_FORMATS,
		.sig_bits = 24,
	},
	.ops = &hdmi_dai_ops,
	.pcm_new = hdmi_codec_pcm_new,
};

static const struct snd_soc_dai_driver hdmi_spdif_dai = {
	.name = "spdif-hifi",
	.id = DAI_ID_SPDIF,
	.playback = {
		.stream_name = "SPDIF Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = HDMI_RATES,
		.formats = SPDIF_FORMATS,
	},
	.ops = &hdmi_dai_ops,
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

static const struct snd_soc_codec_driver hdmi_codec = {
	.component_driver = {
		.dapm_widgets		= hdmi_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(hdmi_widgets),
		.dapm_routes		= hdmi_routes,
		.num_dapm_routes	= ARRAY_SIZE(hdmi_routes),
		.of_xlate_dai_id	= hdmi_of_xlate_dai_id,
	},
};

static int hdmi_codec_probe(struct platform_device *pdev)
{
	struct hdmi_codec_pdata *hcd = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct hdmi_codec_priv *hcp;
	int dai_count, i = 0;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	if (!hcd) {
		dev_err(dev, "%s: No plalform data\n", __func__);
		return -EINVAL;
	}

	dai_count = hcd->i2s + hcd->spdif;
	if (dai_count < 1 || !hcd->ops || !hcd->ops->hw_params ||
	    !hcd->ops->audio_shutdown) {
		dev_err(dev, "%s: Invalid parameters\n", __func__);
		return -EINVAL;
	}

	hcp = devm_kzalloc(dev, sizeof(*hcp), GFP_KERNEL);
	if (!hcp)
		return -ENOMEM;

	hcp->hcd = *hcd;
	mutex_init(&hcp->current_stream_lock);

	hcp->daidrv = devm_kzalloc(dev, dai_count * sizeof(*hcp->daidrv),
				   GFP_KERNEL);
	if (!hcp->daidrv)
		return -ENOMEM;

	if (hcd->i2s) {
		hcp->daidrv[i] = hdmi_i2s_dai;
		hcp->daidrv[i].playback.channels_max =
			hcd->max_i2s_channels;
		i++;
	}

	if (hcd->spdif)
		hcp->daidrv[i] = hdmi_spdif_dai;

	ret = snd_soc_register_codec(dev, &hdmi_codec, hcp->daidrv,
				     dai_count);
	if (ret) {
		dev_err(dev, "%s: snd_soc_register_codec() failed (%d)\n",
			__func__, ret);
		return ret;
	}

	dev_set_drvdata(dev, hcp);
	return 0;
}

static int hdmi_codec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmi_codec_priv *hcp;

	hcp = dev_get_drvdata(dev);
	kfree(hcp->chmap_info);
	snd_soc_unregister_codec(dev);

	return 0;
}

static struct platform_driver hdmi_codec_driver = {
	.driver = {
		.name = HDMI_CODEC_DRV_NAME,
	},
	.probe = hdmi_codec_probe,
	.remove = hdmi_codec_remove,
};

module_platform_driver(hdmi_codec_driver);

MODULE_AUTHOR("Jyri Sarha <jsarha@ti.com>");
MODULE_DESCRIPTION("HDMI Audio Codec Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" HDMI_CODEC_DRV_NAME);
