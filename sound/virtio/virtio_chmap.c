// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#include <linux/virtio_config.h>

#include "virtio_card.h"

/* VirtIO->ALSA channel position map */
static const u8 g_v2a_position_map[] = {
	[VIRTIO_SND_CHMAP_NONE] = SNDRV_CHMAP_UNKNOWN,
	[VIRTIO_SND_CHMAP_NA] = SNDRV_CHMAP_NA,
	[VIRTIO_SND_CHMAP_MONO] = SNDRV_CHMAP_MONO,
	[VIRTIO_SND_CHMAP_FL] = SNDRV_CHMAP_FL,
	[VIRTIO_SND_CHMAP_FR] = SNDRV_CHMAP_FR,
	[VIRTIO_SND_CHMAP_RL] = SNDRV_CHMAP_RL,
	[VIRTIO_SND_CHMAP_RR] = SNDRV_CHMAP_RR,
	[VIRTIO_SND_CHMAP_FC] = SNDRV_CHMAP_FC,
	[VIRTIO_SND_CHMAP_LFE] = SNDRV_CHMAP_LFE,
	[VIRTIO_SND_CHMAP_SL] = SNDRV_CHMAP_SL,
	[VIRTIO_SND_CHMAP_SR] = SNDRV_CHMAP_SR,
	[VIRTIO_SND_CHMAP_RC] = SNDRV_CHMAP_RC,
	[VIRTIO_SND_CHMAP_FLC] = SNDRV_CHMAP_FLC,
	[VIRTIO_SND_CHMAP_FRC] = SNDRV_CHMAP_FRC,
	[VIRTIO_SND_CHMAP_RLC] = SNDRV_CHMAP_RLC,
	[VIRTIO_SND_CHMAP_RRC] = SNDRV_CHMAP_RRC,
	[VIRTIO_SND_CHMAP_FLW] = SNDRV_CHMAP_FLW,
	[VIRTIO_SND_CHMAP_FRW] = SNDRV_CHMAP_FRW,
	[VIRTIO_SND_CHMAP_FLH] = SNDRV_CHMAP_FLH,
	[VIRTIO_SND_CHMAP_FCH] = SNDRV_CHMAP_FCH,
	[VIRTIO_SND_CHMAP_FRH] = SNDRV_CHMAP_FRH,
	[VIRTIO_SND_CHMAP_TC] = SNDRV_CHMAP_TC,
	[VIRTIO_SND_CHMAP_TFL] = SNDRV_CHMAP_TFL,
	[VIRTIO_SND_CHMAP_TFR] = SNDRV_CHMAP_TFR,
	[VIRTIO_SND_CHMAP_TFC] = SNDRV_CHMAP_TFC,
	[VIRTIO_SND_CHMAP_TRL] = SNDRV_CHMAP_TRL,
	[VIRTIO_SND_CHMAP_TRR] = SNDRV_CHMAP_TRR,
	[VIRTIO_SND_CHMAP_TRC] = SNDRV_CHMAP_TRC,
	[VIRTIO_SND_CHMAP_TFLC] = SNDRV_CHMAP_TFLC,
	[VIRTIO_SND_CHMAP_TFRC] = SNDRV_CHMAP_TFRC,
	[VIRTIO_SND_CHMAP_TSL] = SNDRV_CHMAP_TSL,
	[VIRTIO_SND_CHMAP_TSR] = SNDRV_CHMAP_TSR,
	[VIRTIO_SND_CHMAP_LLFE] = SNDRV_CHMAP_LLFE,
	[VIRTIO_SND_CHMAP_RLFE] = SNDRV_CHMAP_RLFE,
	[VIRTIO_SND_CHMAP_BC] = SNDRV_CHMAP_BC,
	[VIRTIO_SND_CHMAP_BLC] = SNDRV_CHMAP_BLC,
	[VIRTIO_SND_CHMAP_BRC] = SNDRV_CHMAP_BRC
};

/**
 * virtsnd_chmap_parse_cfg() - Parse the channel map configuration.
 * @snd: VirtIO sound device.
 *
 * This function is called during initial device initialization.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
int virtsnd_chmap_parse_cfg(struct virtio_snd *snd)
{
	struct virtio_device *vdev = snd->vdev;
	u32 i;
	int rc;

	virtio_cread_le(vdev, struct virtio_snd_config, chmaps, &snd->nchmaps);
	if (!snd->nchmaps)
		return 0;

	snd->chmaps = devm_kcalloc(&vdev->dev, snd->nchmaps,
				   sizeof(*snd->chmaps), GFP_KERNEL);
	if (!snd->chmaps)
		return -ENOMEM;

	rc = virtsnd_ctl_query_info(snd, VIRTIO_SND_R_CHMAP_INFO, 0,
				    snd->nchmaps, sizeof(*snd->chmaps),
				    snd->chmaps);
	if (rc)
		return rc;

	/* Count the number of channel maps per each PCM device/stream. */
	for (i = 0; i < snd->nchmaps; ++i) {
		struct virtio_snd_chmap_info *info = &snd->chmaps[i];
		u32 nid = le32_to_cpu(info->hdr.hda_fn_nid);
		struct virtio_pcm *vpcm;
		struct virtio_pcm_stream *vs;

		vpcm = virtsnd_pcm_find_or_create(snd, nid);
		if (IS_ERR(vpcm))
			return PTR_ERR(vpcm);

		switch (info->direction) {
		case VIRTIO_SND_D_OUTPUT:
			vs = &vpcm->streams[SNDRV_PCM_STREAM_PLAYBACK];
			break;
		case VIRTIO_SND_D_INPUT:
			vs = &vpcm->streams[SNDRV_PCM_STREAM_CAPTURE];
			break;
		default:
			dev_err(&vdev->dev,
				"chmap #%u: unknown direction (%u)\n", i,
				info->direction);
			return -EINVAL;
		}

		vs->nchmaps++;
	}

	return 0;
}

/**
 * virtsnd_chmap_add_ctls() - Create an ALSA control for channel maps.
 * @pcm: ALSA PCM device.
 * @direction: PCM stream direction (SNDRV_PCM_STREAM_XXX).
 * @vs: VirtIO PCM stream.
 *
 * Context: Any context.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_chmap_add_ctls(struct snd_pcm *pcm, int direction,
				  struct virtio_pcm_stream *vs)
{
	u32 i;
	int max_channels = 0;

	for (i = 0; i < vs->nchmaps; i++)
		if (max_channels < vs->chmaps[i].channels)
			max_channels = vs->chmaps[i].channels;

	return snd_pcm_add_chmap_ctls(pcm, direction, vs->chmaps, max_channels,
				      0, NULL);
}

/**
 * virtsnd_chmap_build_devs() - Build ALSA controls for channel maps.
 * @snd: VirtIO sound device.
 *
 * Context: Any context.
 * Return: 0 on success, -errno on failure.
 */
int virtsnd_chmap_build_devs(struct virtio_snd *snd)
{
	struct virtio_device *vdev = snd->vdev;
	struct virtio_pcm *vpcm;
	struct virtio_pcm_stream *vs;
	u32 i;
	int rc;

	/* Allocate channel map elements per each PCM device/stream. */
	list_for_each_entry(vpcm, &snd->pcm_list, list) {
		for (i = 0; i < ARRAY_SIZE(vpcm->streams); ++i) {
			vs = &vpcm->streams[i];

			if (!vs->nchmaps)
				continue;

			vs->chmaps = devm_kcalloc(&vdev->dev, vs->nchmaps + 1,
						  sizeof(*vs->chmaps),
						  GFP_KERNEL);
			if (!vs->chmaps)
				return -ENOMEM;

			vs->nchmaps = 0;
		}
	}

	/* Initialize channel maps per each PCM device/stream. */
	for (i = 0; i < snd->nchmaps; ++i) {
		struct virtio_snd_chmap_info *info = &snd->chmaps[i];
		unsigned int channels = info->channels;
		unsigned int ch;
		struct snd_pcm_chmap_elem *chmap;

		vpcm = virtsnd_pcm_find(snd, le32_to_cpu(info->hdr.hda_fn_nid));
		if (IS_ERR(vpcm))
			return PTR_ERR(vpcm);

		if (info->direction == VIRTIO_SND_D_OUTPUT)
			vs = &vpcm->streams[SNDRV_PCM_STREAM_PLAYBACK];
		else
			vs = &vpcm->streams[SNDRV_PCM_STREAM_CAPTURE];

		chmap = &vs->chmaps[vs->nchmaps++];

		if (channels > ARRAY_SIZE(chmap->map))
			channels = ARRAY_SIZE(chmap->map);

		chmap->channels = channels;

		for (ch = 0; ch < channels; ++ch) {
			u8 position = info->positions[ch];

			if (position >= ARRAY_SIZE(g_v2a_position_map))
				return -EINVAL;

			chmap->map[ch] = g_v2a_position_map[position];
		}
	}

	/* Create an ALSA control per each PCM device/stream. */
	list_for_each_entry(vpcm, &snd->pcm_list, list) {
		if (!vpcm->pcm)
			continue;

		for (i = 0; i < ARRAY_SIZE(vpcm->streams); ++i) {
			vs = &vpcm->streams[i];

			if (!vs->nchmaps)
				continue;

			rc = virtsnd_chmap_add_ctls(vpcm->pcm, i, vs);
			if (rc)
				return rc;
		}
	}

	return 0;
}
