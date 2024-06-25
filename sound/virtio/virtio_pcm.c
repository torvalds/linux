// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#include <linux/moduleparam.h>
#include <linux/virtio_config.h>

#include "virtio_card.h"

static u32 pcm_buffer_ms = 160;
module_param(pcm_buffer_ms, uint, 0644);
MODULE_PARM_DESC(pcm_buffer_ms, "PCM substream buffer time in milliseconds");

static u32 pcm_periods_min = 2;
module_param(pcm_periods_min, uint, 0644);
MODULE_PARM_DESC(pcm_periods_min, "Minimum number of PCM periods");

static u32 pcm_periods_max = 16;
module_param(pcm_periods_max, uint, 0644);
MODULE_PARM_DESC(pcm_periods_max, "Maximum number of PCM periods");

static u32 pcm_period_ms_min = 10;
module_param(pcm_period_ms_min, uint, 0644);
MODULE_PARM_DESC(pcm_period_ms_min, "Minimum PCM period time in milliseconds");

static u32 pcm_period_ms_max = 80;
module_param(pcm_period_ms_max, uint, 0644);
MODULE_PARM_DESC(pcm_period_ms_max, "Maximum PCM period time in milliseconds");

/* Map for converting VirtIO format to ALSA format. */
static const snd_pcm_format_t g_v2a_format_map[] = {
	[VIRTIO_SND_PCM_FMT_IMA_ADPCM] = SNDRV_PCM_FORMAT_IMA_ADPCM,
	[VIRTIO_SND_PCM_FMT_MU_LAW] = SNDRV_PCM_FORMAT_MU_LAW,
	[VIRTIO_SND_PCM_FMT_A_LAW] = SNDRV_PCM_FORMAT_A_LAW,
	[VIRTIO_SND_PCM_FMT_S8] = SNDRV_PCM_FORMAT_S8,
	[VIRTIO_SND_PCM_FMT_U8] = SNDRV_PCM_FORMAT_U8,
	[VIRTIO_SND_PCM_FMT_S16] = SNDRV_PCM_FORMAT_S16_LE,
	[VIRTIO_SND_PCM_FMT_U16] = SNDRV_PCM_FORMAT_U16_LE,
	[VIRTIO_SND_PCM_FMT_S18_3] = SNDRV_PCM_FORMAT_S18_3LE,
	[VIRTIO_SND_PCM_FMT_U18_3] = SNDRV_PCM_FORMAT_U18_3LE,
	[VIRTIO_SND_PCM_FMT_S20_3] = SNDRV_PCM_FORMAT_S20_3LE,
	[VIRTIO_SND_PCM_FMT_U20_3] = SNDRV_PCM_FORMAT_U20_3LE,
	[VIRTIO_SND_PCM_FMT_S24_3] = SNDRV_PCM_FORMAT_S24_3LE,
	[VIRTIO_SND_PCM_FMT_U24_3] = SNDRV_PCM_FORMAT_U24_3LE,
	[VIRTIO_SND_PCM_FMT_S20] = SNDRV_PCM_FORMAT_S20_LE,
	[VIRTIO_SND_PCM_FMT_U20] = SNDRV_PCM_FORMAT_U20_LE,
	[VIRTIO_SND_PCM_FMT_S24] = SNDRV_PCM_FORMAT_S24_LE,
	[VIRTIO_SND_PCM_FMT_U24] = SNDRV_PCM_FORMAT_U24_LE,
	[VIRTIO_SND_PCM_FMT_S32] = SNDRV_PCM_FORMAT_S32_LE,
	[VIRTIO_SND_PCM_FMT_U32] = SNDRV_PCM_FORMAT_U32_LE,
	[VIRTIO_SND_PCM_FMT_FLOAT] = SNDRV_PCM_FORMAT_FLOAT_LE,
	[VIRTIO_SND_PCM_FMT_FLOAT64] = SNDRV_PCM_FORMAT_FLOAT64_LE,
	[VIRTIO_SND_PCM_FMT_DSD_U8] = SNDRV_PCM_FORMAT_DSD_U8,
	[VIRTIO_SND_PCM_FMT_DSD_U16] = SNDRV_PCM_FORMAT_DSD_U16_LE,
	[VIRTIO_SND_PCM_FMT_DSD_U32] = SNDRV_PCM_FORMAT_DSD_U32_LE,
	[VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME] =
		SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE
};

/* Map for converting VirtIO frame rate to ALSA frame rate. */
struct virtsnd_v2a_rate {
	unsigned int alsa_bit;
	unsigned int rate;
};

static const struct virtsnd_v2a_rate g_v2a_rate_map[] = {
	[VIRTIO_SND_PCM_RATE_5512] = { SNDRV_PCM_RATE_5512, 5512 },
	[VIRTIO_SND_PCM_RATE_8000] = { SNDRV_PCM_RATE_8000, 8000 },
	[VIRTIO_SND_PCM_RATE_11025] = { SNDRV_PCM_RATE_11025, 11025 },
	[VIRTIO_SND_PCM_RATE_16000] = { SNDRV_PCM_RATE_16000, 16000 },
	[VIRTIO_SND_PCM_RATE_22050] = { SNDRV_PCM_RATE_22050, 22050 },
	[VIRTIO_SND_PCM_RATE_32000] = { SNDRV_PCM_RATE_32000, 32000 },
	[VIRTIO_SND_PCM_RATE_44100] = { SNDRV_PCM_RATE_44100, 44100 },
	[VIRTIO_SND_PCM_RATE_48000] = { SNDRV_PCM_RATE_48000, 48000 },
	[VIRTIO_SND_PCM_RATE_64000] = { SNDRV_PCM_RATE_64000, 64000 },
	[VIRTIO_SND_PCM_RATE_88200] = { SNDRV_PCM_RATE_88200, 88200 },
	[VIRTIO_SND_PCM_RATE_96000] = { SNDRV_PCM_RATE_96000, 96000 },
	[VIRTIO_SND_PCM_RATE_176400] = { SNDRV_PCM_RATE_176400, 176400 },
	[VIRTIO_SND_PCM_RATE_192000] = { SNDRV_PCM_RATE_192000, 192000 }
};

/**
 * virtsnd_pcm_build_hw() - Parse substream config and build HW descriptor.
 * @vss: VirtIO substream.
 * @info: VirtIO substream information entry.
 *
 * Context: Any context.
 * Return: 0 on success, -EINVAL if configuration is invalid.
 */
static int virtsnd_pcm_build_hw(struct virtio_pcm_substream *vss,
				struct virtio_snd_pcm_info *info)
{
	struct virtio_device *vdev = vss->snd->vdev;
	unsigned int i;
	u64 values;
	size_t sample_max = 0;
	size_t sample_min = 0;

	vss->features = le32_to_cpu(info->features);

	/*
	 * TODO: set SNDRV_PCM_INFO_{BATCH,BLOCK_TRANSFER} if device supports
	 * only message-based transport.
	 */
	vss->hw.info =
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_NO_REWINDS |
		SNDRV_PCM_INFO_SYNC_APPLPTR;

	if (!info->channels_min || info->channels_min > info->channels_max) {
		dev_err(&vdev->dev,
			"SID %u: invalid channel range [%u %u]\n",
			vss->sid, info->channels_min, info->channels_max);
		return -EINVAL;
	}

	vss->hw.channels_min = info->channels_min;
	vss->hw.channels_max = info->channels_max;

	values = le64_to_cpu(info->formats);

	vss->hw.formats = 0;

	for (i = 0; i < ARRAY_SIZE(g_v2a_format_map); ++i)
		if (values & (1ULL << i)) {
			snd_pcm_format_t alsa_fmt = g_v2a_format_map[i];
			int bytes = snd_pcm_format_physical_width(alsa_fmt) / 8;

			if (!sample_min || sample_min > bytes)
				sample_min = bytes;

			if (sample_max < bytes)
				sample_max = bytes;

			vss->hw.formats |= pcm_format_to_bits(alsa_fmt);
		}

	if (!vss->hw.formats) {
		dev_err(&vdev->dev,
			"SID %u: no supported PCM sample formats found\n",
			vss->sid);
		return -EINVAL;
	}

	values = le64_to_cpu(info->rates);

	vss->hw.rates = 0;

	for (i = 0; i < ARRAY_SIZE(g_v2a_rate_map); ++i)
		if (values & (1ULL << i)) {
			if (!vss->hw.rate_min ||
			    vss->hw.rate_min > g_v2a_rate_map[i].rate)
				vss->hw.rate_min = g_v2a_rate_map[i].rate;

			if (vss->hw.rate_max < g_v2a_rate_map[i].rate)
				vss->hw.rate_max = g_v2a_rate_map[i].rate;

			vss->hw.rates |= g_v2a_rate_map[i].alsa_bit;
		}

	if (!vss->hw.rates) {
		dev_err(&vdev->dev,
			"SID %u: no supported PCM frame rates found\n",
			vss->sid);
		return -EINVAL;
	}

	vss->hw.periods_min = pcm_periods_min;
	vss->hw.periods_max = pcm_periods_max;

	/*
	 * We must ensure that there is enough space in the buffer to store
	 * pcm_buffer_ms ms for the combination (Cmax, Smax, Rmax), where:
	 *   Cmax = maximum supported number of channels,
	 *   Smax = maximum supported sample size in bytes,
	 *   Rmax = maximum supported frame rate.
	 */
	vss->hw.buffer_bytes_max =
		PAGE_ALIGN(sample_max * vss->hw.channels_max * pcm_buffer_ms *
			   (vss->hw.rate_max / MSEC_PER_SEC));

	/*
	 * We must ensure that the minimum period size is enough to store
	 * pcm_period_ms_min ms for the combination (Cmin, Smin, Rmin), where:
	 *   Cmin = minimum supported number of channels,
	 *   Smin = minimum supported sample size in bytes,
	 *   Rmin = minimum supported frame rate.
	 */
	vss->hw.period_bytes_min =
		sample_min * vss->hw.channels_min * pcm_period_ms_min *
		(vss->hw.rate_min / MSEC_PER_SEC);

	/*
	 * We must ensure that the maximum period size is enough to store
	 * pcm_period_ms_max ms for the combination (Cmax, Smax, Rmax).
	 */
	vss->hw.period_bytes_max =
		sample_max * vss->hw.channels_max * pcm_period_ms_max *
		(vss->hw.rate_max / MSEC_PER_SEC);

	return 0;
}

/**
 * virtsnd_pcm_find() - Find the PCM device for the specified node ID.
 * @snd: VirtIO sound device.
 * @nid: Function node ID.
 *
 * Context: Any context.
 * Return: a pointer to the PCM device or ERR_PTR(-ENOENT).
 */
struct virtio_pcm *virtsnd_pcm_find(struct virtio_snd *snd, u32 nid)
{
	struct virtio_pcm *vpcm;

	list_for_each_entry(vpcm, &snd->pcm_list, list)
		if (vpcm->nid == nid)
			return vpcm;

	return ERR_PTR(-ENOENT);
}

/**
 * virtsnd_pcm_find_or_create() - Find or create the PCM device for the
 *                                specified node ID.
 * @snd: VirtIO sound device.
 * @nid: Function node ID.
 *
 * Context: Any context that permits to sleep.
 * Return: a pointer to the PCM device or ERR_PTR(-errno).
 */
struct virtio_pcm *virtsnd_pcm_find_or_create(struct virtio_snd *snd, u32 nid)
{
	struct virtio_device *vdev = snd->vdev;
	struct virtio_pcm *vpcm;

	vpcm = virtsnd_pcm_find(snd, nid);
	if (!IS_ERR(vpcm))
		return vpcm;

	vpcm = devm_kzalloc(&vdev->dev, sizeof(*vpcm), GFP_KERNEL);
	if (!vpcm)
		return ERR_PTR(-ENOMEM);

	vpcm->nid = nid;
	list_add_tail(&vpcm->list, &snd->pcm_list);

	return vpcm;
}

/**
 * virtsnd_pcm_validate() - Validate if the device can be started.
 * @vdev: VirtIO parent device.
 *
 * Context: Any context.
 * Return: 0 on success, -EINVAL on failure.
 */
int virtsnd_pcm_validate(struct virtio_device *vdev)
{
	if (pcm_periods_min < 2 || pcm_periods_min > pcm_periods_max) {
		dev_err(&vdev->dev,
			"invalid range [%u %u] of the number of PCM periods\n",
			pcm_periods_min, pcm_periods_max);
		return -EINVAL;
	}

	if (!pcm_period_ms_min || pcm_period_ms_min > pcm_period_ms_max) {
		dev_err(&vdev->dev,
			"invalid range [%u %u] of the size of the PCM period\n",
			pcm_period_ms_min, pcm_period_ms_max);
		return -EINVAL;
	}

	if (pcm_buffer_ms < pcm_periods_min * pcm_period_ms_min) {
		dev_err(&vdev->dev,
			"pcm_buffer_ms(=%u) value cannot be < %u ms\n",
			pcm_buffer_ms, pcm_periods_min * pcm_period_ms_min);
		return -EINVAL;
	}

	if (pcm_period_ms_max > pcm_buffer_ms / 2) {
		dev_err(&vdev->dev,
			"pcm_period_ms_max(=%u) value cannot be > %u ms\n",
			pcm_period_ms_max, pcm_buffer_ms / 2);
		return -EINVAL;
	}

	return 0;
}

/**
 * virtsnd_pcm_period_elapsed() - Kernel work function to handle the elapsed
 *                                period state.
 * @work: Elapsed period work.
 *
 * The main purpose of this function is to call snd_pcm_period_elapsed() in
 * a process context, not in an interrupt context. This is necessary because PCM
 * devices operate in non-atomic mode.
 *
 * Context: Process context.
 */
static void virtsnd_pcm_period_elapsed(struct work_struct *work)
{
	struct virtio_pcm_substream *vss =
		container_of(work, struct virtio_pcm_substream, elapsed_period);

	snd_pcm_period_elapsed(vss->substream);
}

/**
 * virtsnd_pcm_parse_cfg() - Parse the stream configuration.
 * @snd: VirtIO sound device.
 *
 * This function is called during initial device initialization.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
int virtsnd_pcm_parse_cfg(struct virtio_snd *snd)
{
	struct virtio_device *vdev = snd->vdev;
	struct virtio_snd_pcm_info *info;
	u32 i;
	int rc;

	virtio_cread_le(vdev, struct virtio_snd_config, streams,
			&snd->nsubstreams);
	if (!snd->nsubstreams)
		return 0;

	snd->substreams = devm_kcalloc(&vdev->dev, snd->nsubstreams,
				       sizeof(*snd->substreams), GFP_KERNEL);
	if (!snd->substreams)
		return -ENOMEM;

	info = kcalloc(snd->nsubstreams, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	rc = virtsnd_ctl_query_info(snd, VIRTIO_SND_R_PCM_INFO, 0,
				    snd->nsubstreams, sizeof(*info), info);
	if (rc)
		goto on_exit;

	for (i = 0; i < snd->nsubstreams; ++i) {
		struct virtio_pcm_substream *vss = &snd->substreams[i];
		struct virtio_pcm *vpcm;

		vss->snd = snd;
		vss->sid = i;
		INIT_WORK(&vss->elapsed_period, virtsnd_pcm_period_elapsed);
		init_waitqueue_head(&vss->msg_empty);
		spin_lock_init(&vss->lock);

		rc = virtsnd_pcm_build_hw(vss, &info[i]);
		if (rc)
			goto on_exit;

		vss->nid = le32_to_cpu(info[i].hdr.hda_fn_nid);

		vpcm = virtsnd_pcm_find_or_create(snd, vss->nid);
		if (IS_ERR(vpcm)) {
			rc = PTR_ERR(vpcm);
			goto on_exit;
		}

		switch (info[i].direction) {
		case VIRTIO_SND_D_OUTPUT:
			vss->direction = SNDRV_PCM_STREAM_PLAYBACK;
			break;
		case VIRTIO_SND_D_INPUT:
			vss->direction = SNDRV_PCM_STREAM_CAPTURE;
			break;
		default:
			dev_err(&vdev->dev, "SID %u: unknown direction (%u)\n",
				vss->sid, info[i].direction);
			rc = -EINVAL;
			goto on_exit;
		}

		vpcm->streams[vss->direction].nsubstreams++;
	}

on_exit:
	kfree(info);

	return rc;
}

/**
 * virtsnd_pcm_build_devs() - Build ALSA PCM devices.
 * @snd: VirtIO sound device.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
int virtsnd_pcm_build_devs(struct virtio_snd *snd)
{
	struct virtio_device *vdev = snd->vdev;
	struct virtio_pcm *vpcm;
	u32 i;
	int rc;

	list_for_each_entry(vpcm, &snd->pcm_list, list) {
		unsigned int npbs =
			vpcm->streams[SNDRV_PCM_STREAM_PLAYBACK].nsubstreams;
		unsigned int ncps =
			vpcm->streams[SNDRV_PCM_STREAM_CAPTURE].nsubstreams;

		if (!npbs && !ncps)
			continue;

		rc = snd_pcm_new(snd->card, VIRTIO_SND_CARD_DRIVER, vpcm->nid,
				 npbs, ncps, &vpcm->pcm);
		if (rc) {
			dev_err(&vdev->dev, "snd_pcm_new[%u] failed: %d\n",
				vpcm->nid, rc);
			return rc;
		}

		vpcm->pcm->info_flags = 0;
		vpcm->pcm->dev_class = SNDRV_PCM_CLASS_GENERIC;
		vpcm->pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
		snprintf(vpcm->pcm->name, sizeof(vpcm->pcm->name),
			 VIRTIO_SND_PCM_NAME " %u", vpcm->pcm->device);
		vpcm->pcm->private_data = vpcm;
		vpcm->pcm->nonatomic = true;

		for (i = 0; i < ARRAY_SIZE(vpcm->streams); ++i) {
			struct virtio_pcm_stream *stream = &vpcm->streams[i];

			if (!stream->nsubstreams)
				continue;

			stream->substreams =
				devm_kcalloc(&vdev->dev, stream->nsubstreams,
					     sizeof(*stream->substreams),
					     GFP_KERNEL);
			if (!stream->substreams)
				return -ENOMEM;

			stream->nsubstreams = 0;
		}
	}

	for (i = 0; i < snd->nsubstreams; ++i) {
		struct virtio_pcm_stream *vs;
		struct virtio_pcm_substream *vss = &snd->substreams[i];

		vpcm = virtsnd_pcm_find(snd, vss->nid);
		if (IS_ERR(vpcm))
			return PTR_ERR(vpcm);

		vs = &vpcm->streams[vss->direction];
		vs->substreams[vs->nsubstreams++] = vss;
	}

	list_for_each_entry(vpcm, &snd->pcm_list, list) {
		for (i = 0; i < ARRAY_SIZE(vpcm->streams); ++i) {
			struct virtio_pcm_stream *vs = &vpcm->streams[i];
			struct snd_pcm_str *ks = &vpcm->pcm->streams[i];
			struct snd_pcm_substream *kss;

			if (!vs->nsubstreams)
				continue;

			for (kss = ks->substream; kss; kss = kss->next)
				vs->substreams[kss->number]->substream = kss;

			snd_pcm_set_ops(vpcm->pcm, i, &virtsnd_pcm_ops[i]);
		}

		snd_pcm_set_managed_buffer_all(vpcm->pcm,
					       SNDRV_DMA_TYPE_VMALLOC, NULL,
					       0, 0);
	}

	return 0;
}

/**
 * virtsnd_pcm_event() - Handle the PCM device event notification.
 * @snd: VirtIO sound device.
 * @event: VirtIO sound event.
 *
 * Context: Interrupt context.
 */
void virtsnd_pcm_event(struct virtio_snd *snd, struct virtio_snd_event *event)
{
	struct virtio_pcm_substream *vss;
	u32 sid = le32_to_cpu(event->data);

	if (sid >= snd->nsubstreams)
		return;

	vss = &snd->substreams[sid];

	switch (le32_to_cpu(event->hdr.code)) {
	case VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED:
		/* TODO: deal with shmem elapsed period */
		break;
	case VIRTIO_SND_EVT_PCM_XRUN:
		spin_lock(&vss->lock);
		if (vss->xfer_enabled)
			vss->xfer_xrun = true;
		spin_unlock(&vss->lock);
		break;
	}
}
