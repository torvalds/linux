// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#include <sound/pcm_params.h>

#include "virtio_card.h"

/*
 * I/O messages lifetime
 * ---------------------
 *
 * Allocation:
 *   Messages are initially allocated in the ops->hw_params() after the size and
 *   number of periods have been successfully negotiated.
 *
 * Freeing:
 *   Messages can be safely freed after the queue has been successfully flushed
 *   (RELEASE command in the ops->sync_stop()) and the ops->hw_free() has been
 *   called.
 *
 *   When the substream stops, the ops->sync_stop() waits until the device has
 *   completed all pending messages. This wait can be interrupted either by a
 *   signal or due to a timeout. In this case, the device can still access
 *   messages even after calling ops->hw_free(). It can also issue an interrupt,
 *   and the interrupt handler will also try to access message structures.
 *
 *   Therefore, freeing of already allocated messages occurs:
 *
 *   - in ops->hw_params(), if this operator was called several times in a row,
 *     or if ops->hw_free() failed to free messages previously;
 *
 *   - in ops->hw_free(), if the queue has been successfully flushed;
 *
 *   - in dev->release().
 */

/* Map for converting ALSA format to VirtIO format. */
struct virtsnd_a2v_format {
	snd_pcm_format_t alsa_bit;
	unsigned int vio_bit;
};

static const struct virtsnd_a2v_format g_a2v_format_map[] = {
	{ SNDRV_PCM_FORMAT_IMA_ADPCM, VIRTIO_SND_PCM_FMT_IMA_ADPCM },
	{ SNDRV_PCM_FORMAT_MU_LAW, VIRTIO_SND_PCM_FMT_MU_LAW },
	{ SNDRV_PCM_FORMAT_A_LAW, VIRTIO_SND_PCM_FMT_A_LAW },
	{ SNDRV_PCM_FORMAT_S8, VIRTIO_SND_PCM_FMT_S8 },
	{ SNDRV_PCM_FORMAT_U8, VIRTIO_SND_PCM_FMT_U8 },
	{ SNDRV_PCM_FORMAT_S16_LE, VIRTIO_SND_PCM_FMT_S16 },
	{ SNDRV_PCM_FORMAT_U16_LE, VIRTIO_SND_PCM_FMT_U16 },
	{ SNDRV_PCM_FORMAT_S18_3LE, VIRTIO_SND_PCM_FMT_S18_3 },
	{ SNDRV_PCM_FORMAT_U18_3LE, VIRTIO_SND_PCM_FMT_U18_3 },
	{ SNDRV_PCM_FORMAT_S20_3LE, VIRTIO_SND_PCM_FMT_S20_3 },
	{ SNDRV_PCM_FORMAT_U20_3LE, VIRTIO_SND_PCM_FMT_U20_3 },
	{ SNDRV_PCM_FORMAT_S24_3LE, VIRTIO_SND_PCM_FMT_S24_3 },
	{ SNDRV_PCM_FORMAT_U24_3LE, VIRTIO_SND_PCM_FMT_U24_3 },
	{ SNDRV_PCM_FORMAT_S20_LE, VIRTIO_SND_PCM_FMT_S20 },
	{ SNDRV_PCM_FORMAT_U20_LE, VIRTIO_SND_PCM_FMT_U20 },
	{ SNDRV_PCM_FORMAT_S24_LE, VIRTIO_SND_PCM_FMT_S24 },
	{ SNDRV_PCM_FORMAT_U24_LE, VIRTIO_SND_PCM_FMT_U24 },
	{ SNDRV_PCM_FORMAT_S32_LE, VIRTIO_SND_PCM_FMT_S32 },
	{ SNDRV_PCM_FORMAT_U32_LE, VIRTIO_SND_PCM_FMT_U32 },
	{ SNDRV_PCM_FORMAT_FLOAT_LE, VIRTIO_SND_PCM_FMT_FLOAT },
	{ SNDRV_PCM_FORMAT_FLOAT64_LE, VIRTIO_SND_PCM_FMT_FLOAT64 },
	{ SNDRV_PCM_FORMAT_DSD_U8, VIRTIO_SND_PCM_FMT_DSD_U8 },
	{ SNDRV_PCM_FORMAT_DSD_U16_LE, VIRTIO_SND_PCM_FMT_DSD_U16 },
	{ SNDRV_PCM_FORMAT_DSD_U32_LE, VIRTIO_SND_PCM_FMT_DSD_U32 },
	{ SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE,
	  VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME }
};

/* Map for converting ALSA frame rate to VirtIO frame rate. */
struct virtsnd_a2v_rate {
	unsigned int rate;
	unsigned int vio_bit;
};

static const struct virtsnd_a2v_rate g_a2v_rate_map[] = {
	{ 5512, VIRTIO_SND_PCM_RATE_5512 },
	{ 8000, VIRTIO_SND_PCM_RATE_8000 },
	{ 11025, VIRTIO_SND_PCM_RATE_11025 },
	{ 16000, VIRTIO_SND_PCM_RATE_16000 },
	{ 22050, VIRTIO_SND_PCM_RATE_22050 },
	{ 32000, VIRTIO_SND_PCM_RATE_32000 },
	{ 44100, VIRTIO_SND_PCM_RATE_44100 },
	{ 48000, VIRTIO_SND_PCM_RATE_48000 },
	{ 64000, VIRTIO_SND_PCM_RATE_64000 },
	{ 88200, VIRTIO_SND_PCM_RATE_88200 },
	{ 96000, VIRTIO_SND_PCM_RATE_96000 },
	{ 176400, VIRTIO_SND_PCM_RATE_176400 },
	{ 192000, VIRTIO_SND_PCM_RATE_192000 }
};

static int virtsnd_pcm_sync_stop(struct snd_pcm_substream *substream);

/**
 * virtsnd_pcm_open() - Open the PCM substream.
 * @substream: Kernel ALSA substream.
 *
 * Context: Process context.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_pcm_open(struct snd_pcm_substream *substream)
{
	struct virtio_pcm *vpcm = snd_pcm_substream_chip(substream);
	struct virtio_pcm_stream *vs = &vpcm->streams[substream->stream];
	struct virtio_pcm_substream *vss = vs->substreams[substream->number];

	substream->runtime->hw = vss->hw;
	substream->private_data = vss;

	snd_pcm_hw_constraint_integer(substream->runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);

	vss->stopped = !!virtsnd_pcm_msg_pending_num(vss);
	vss->suspended = false;

	/*
	 * If the substream has already been used, then the I/O queue may be in
	 * an invalid state. Just in case, we do a check and try to return the
	 * queue to its original state, if necessary.
	 */
	return virtsnd_pcm_sync_stop(substream);
}

/**
 * virtsnd_pcm_close() - Close the PCM substream.
 * @substream: Kernel ALSA substream.
 *
 * Context: Process context.
 * Return: 0.
 */
static int virtsnd_pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/**
 * virtsnd_pcm_dev_set_params() - Set the parameters of the PCM substream on
 *                                the device side.
 * @vss: VirtIO PCM substream.
 * @buffer_bytes: Size of the hardware buffer.
 * @period_bytes: Size of the hardware period.
 * @channels: Selected number of channels.
 * @format: Selected sample format (SNDRV_PCM_FORMAT_XXX).
 * @rate: Selected frame rate.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_pcm_dev_set_params(struct virtio_pcm_substream *vss,
				      unsigned int buffer_bytes,
				      unsigned int period_bytes,
				      unsigned int channels,
				      snd_pcm_format_t format,
				      unsigned int rate)
{
	struct virtio_snd_msg *msg;
	struct virtio_snd_pcm_set_params *request;
	unsigned int i;
	int vformat = -1;
	int vrate = -1;

	for (i = 0; i < ARRAY_SIZE(g_a2v_format_map); ++i)
		if (g_a2v_format_map[i].alsa_bit == format) {
			vformat = g_a2v_format_map[i].vio_bit;

			break;
		}

	for (i = 0; i < ARRAY_SIZE(g_a2v_rate_map); ++i)
		if (g_a2v_rate_map[i].rate == rate) {
			vrate = g_a2v_rate_map[i].vio_bit;

			break;
		}

	if (vformat == -1 || vrate == -1)
		return -EINVAL;

	msg = virtsnd_pcm_ctl_msg_alloc(vss, VIRTIO_SND_R_PCM_SET_PARAMS,
					GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	request = virtsnd_ctl_msg_request(msg);
	request->buffer_bytes = cpu_to_le32(buffer_bytes);
	request->period_bytes = cpu_to_le32(period_bytes);
	request->channels = channels;
	request->format = vformat;
	request->rate = vrate;

	if (vss->features & (1U << VIRTIO_SND_PCM_F_MSG_POLLING))
		request->features |=
			cpu_to_le32(1U << VIRTIO_SND_PCM_F_MSG_POLLING);

	if (vss->features & (1U << VIRTIO_SND_PCM_F_EVT_XRUNS))
		request->features |=
			cpu_to_le32(1U << VIRTIO_SND_PCM_F_EVT_XRUNS);

	return virtsnd_ctl_msg_send_sync(vss->snd, msg);
}

/**
 * virtsnd_pcm_hw_params() - Set the parameters of the PCM substream.
 * @substream: Kernel ALSA substream.
 * @hw_params: Hardware parameters.
 *
 * Context: Process context.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);
	struct virtio_device *vdev = vss->snd->vdev;
	int rc;

	if (virtsnd_pcm_msg_pending_num(vss)) {
		dev_err(&vdev->dev, "SID %u: invalid I/O queue state\n",
			vss->sid);
		return -EBADFD;
	}

	rc = virtsnd_pcm_dev_set_params(vss, params_buffer_bytes(hw_params),
					params_period_bytes(hw_params),
					params_channels(hw_params),
					params_format(hw_params),
					params_rate(hw_params));
	if (rc)
		return rc;

	/*
	 * Free previously allocated messages if ops->hw_params() is called
	 * several times in a row, or if ops->hw_free() failed to free messages.
	 */
	virtsnd_pcm_msg_free(vss);

	return virtsnd_pcm_msg_alloc(vss, params_periods(hw_params),
				     params_period_bytes(hw_params));
}

/**
 * virtsnd_pcm_hw_free() - Reset the parameters of the PCM substream.
 * @substream: Kernel ALSA substream.
 *
 * Context: Process context.
 * Return: 0
 */
static int virtsnd_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);

	/* If the queue is flushed, we can safely free the messages here. */
	if (!virtsnd_pcm_msg_pending_num(vss))
		virtsnd_pcm_msg_free(vss);

	return 0;
}

/**
 * virtsnd_pcm_prepare() - Prepare the PCM substream.
 * @substream: Kernel ALSA substream.
 *
 * Context: Process context.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);
	struct virtio_device *vdev = vss->snd->vdev;
	struct virtio_snd_msg *msg;

	if (!vss->suspended) {
		if (virtsnd_pcm_msg_pending_num(vss)) {
			dev_err(&vdev->dev, "SID %u: invalid I/O queue state\n",
				vss->sid);
			return -EBADFD;
		}

		vss->buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
		vss->hw_ptr = 0;
	} else {
		struct snd_pcm_runtime *runtime = substream->runtime;
		unsigned int buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
		unsigned int period_bytes = snd_pcm_lib_period_bytes(substream);
		int rc;

		rc = virtsnd_pcm_dev_set_params(vss, buffer_bytes, period_bytes,
						runtime->channels,
						runtime->format, runtime->rate);
		if (rc)
			return rc;
	}

	vss->xfer_xrun = false;
	vss->suspended = false;
	vss->msg_count = 0;

	memset(&vss->pcm_indirect, 0, sizeof(vss->pcm_indirect));
	vss->pcm_indirect.sw_buffer_size =
		vss->pcm_indirect.hw_buffer_size =
		snd_pcm_lib_buffer_bytes(substream);

	msg = virtsnd_pcm_ctl_msg_alloc(vss, VIRTIO_SND_R_PCM_PREPARE,
					GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	return virtsnd_ctl_msg_send_sync(vss->snd, msg);
}

/**
 * virtsnd_pcm_trigger() - Process command for the PCM substream.
 * @substream: Kernel ALSA substream.
 * @command: Substream command (SNDRV_PCM_TRIGGER_XXX).
 *
 * Context: Any context. Takes and releases the VirtIO substream spinlock.
 *          May take and release the tx/rx queue spinlock.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_pcm_trigger(struct snd_pcm_substream *substream, int command)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);
	struct virtio_snd *snd = vss->snd;
	struct virtio_snd_queue *queue;
	struct virtio_snd_msg *msg;
	unsigned long flags;
	int rc = 0;

	switch (command) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		queue = virtsnd_pcm_queue(vss);

		spin_lock_irqsave(&queue->lock, flags);
		spin_lock(&vss->lock);
		if (vss->direction == SNDRV_PCM_STREAM_CAPTURE)
			rc = virtsnd_pcm_msg_send(vss, 0, vss->buffer_bytes);
		if (!rc)
			vss->xfer_enabled = true;
		spin_unlock(&vss->lock);
		spin_unlock_irqrestore(&queue->lock, flags);
		if (rc)
			return rc;

		msg = virtsnd_pcm_ctl_msg_alloc(vss, VIRTIO_SND_R_PCM_START,
						GFP_KERNEL);
		if (!msg) {
			spin_lock_irqsave(&vss->lock, flags);
			vss->xfer_enabled = false;
			spin_unlock_irqrestore(&vss->lock, flags);

			return -ENOMEM;
		}

		return virtsnd_ctl_msg_send_sync(snd, msg);
	case SNDRV_PCM_TRIGGER_SUSPEND:
		vss->suspended = true;
		fallthrough;
	case SNDRV_PCM_TRIGGER_STOP:
		vss->stopped = true;
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock_irqsave(&vss->lock, flags);
		vss->xfer_enabled = false;
		spin_unlock_irqrestore(&vss->lock, flags);

		msg = virtsnd_pcm_ctl_msg_alloc(vss, VIRTIO_SND_R_PCM_STOP,
						GFP_KERNEL);
		if (!msg)
			return -ENOMEM;

		return virtsnd_ctl_msg_send_sync(snd, msg);
	default:
		return -EINVAL;
	}
}

/**
 * virtsnd_pcm_sync_stop() - Synchronous PCM substream stop.
 * @substream: Kernel ALSA substream.
 *
 * The function can be called both from the upper level or from the driver
 * itself.
 *
 * Context: Process context. Takes and releases the VirtIO substream spinlock.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_pcm_sync_stop(struct snd_pcm_substream *substream)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);
	struct virtio_snd *snd = vss->snd;
	struct virtio_snd_msg *msg;
	unsigned int js = msecs_to_jiffies(virtsnd_msg_timeout_ms);
	int rc;

	cancel_work_sync(&vss->elapsed_period);

	if (!vss->stopped)
		return 0;

	msg = virtsnd_pcm_ctl_msg_alloc(vss, VIRTIO_SND_R_PCM_RELEASE,
					GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	rc = virtsnd_ctl_msg_send_sync(snd, msg);
	if (rc)
		return rc;

	/*
	 * The spec states that upon receipt of the RELEASE command "the device
	 * MUST complete all pending I/O messages for the specified stream ID".
	 * Thus, we consider the absence of I/O messages in the queue as an
	 * indication that the substream has been released.
	 */
	rc = wait_event_interruptible_timeout(vss->msg_empty,
					      !virtsnd_pcm_msg_pending_num(vss),
					      js);
	if (rc <= 0) {
		dev_warn(&snd->vdev->dev, "SID %u: failed to flush I/O queue\n",
			 vss->sid);

		return !rc ? -ETIMEDOUT : rc;
	}

	vss->stopped = false;

	return 0;
}

/**
 * virtsnd_pcm_pb_pointer() - Get the current hardware position for the PCM
 *                         substream for playback.
 * @substream: Kernel ALSA substream.
 *
 * Context: Any context.
 * Return: Hardware position in frames inside [0 ... buffer_size) range.
 */
static snd_pcm_uframes_t
virtsnd_pcm_pb_pointer(struct snd_pcm_substream *substream)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);

	return snd_pcm_indirect_playback_pointer(substream,
		&vss->pcm_indirect,
		vss->hw_ptr);
}

/**
 * virtsnd_pcm_cp_pointer() - Get the current hardware position for the PCM
 *                         substream for capture.
 * @substream: Kernel ALSA substream.
 *
 * Context: Any context.
 * Return: Hardware position in frames inside [0 ... buffer_size) range.
 */
static snd_pcm_uframes_t
virtsnd_pcm_cp_pointer(struct snd_pcm_substream *substream)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);

	return snd_pcm_indirect_capture_pointer(substream,
		&vss->pcm_indirect,
		vss->hw_ptr);
}

static void virtsnd_pcm_trans_copy(struct snd_pcm_substream *substream,
				   struct snd_pcm_indirect *rec, size_t bytes)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);

	virtsnd_pcm_msg_send(vss, rec->sw_data, bytes);
}

static int virtsnd_pcm_pb_ack(struct snd_pcm_substream *substream)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);
	struct virtio_snd_queue *queue = virtsnd_pcm_queue(vss);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&queue->lock, flags);
	spin_lock(&vss->lock);

	rc = snd_pcm_indirect_playback_transfer(substream, &vss->pcm_indirect,
						virtsnd_pcm_trans_copy);

	spin_unlock(&vss->lock);
	spin_unlock_irqrestore(&queue->lock, flags);

	return rc;
}

static int virtsnd_pcm_cp_ack(struct snd_pcm_substream *substream)
{
	struct virtio_pcm_substream *vss = snd_pcm_substream_chip(substream);
	struct virtio_snd_queue *queue = virtsnd_pcm_queue(vss);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&queue->lock, flags);
	spin_lock(&vss->lock);

	rc = snd_pcm_indirect_capture_transfer(substream, &vss->pcm_indirect,
					       virtsnd_pcm_trans_copy);

	spin_unlock(&vss->lock);
	spin_unlock_irqrestore(&queue->lock, flags);

	return rc;
}

/* PCM substream operators map. */
const struct snd_pcm_ops virtsnd_pcm_ops[] = {
	{
		.open = virtsnd_pcm_open,
		.close = virtsnd_pcm_close,
		.ioctl = snd_pcm_lib_ioctl,
		.hw_params = virtsnd_pcm_hw_params,
		.hw_free = virtsnd_pcm_hw_free,
		.prepare = virtsnd_pcm_prepare,
		.trigger = virtsnd_pcm_trigger,
		.sync_stop = virtsnd_pcm_sync_stop,
		.pointer = virtsnd_pcm_pb_pointer,
		.ack = virtsnd_pcm_pb_ack,
	},
	{
		.open = virtsnd_pcm_open,
		.close = virtsnd_pcm_close,
		.ioctl = snd_pcm_lib_ioctl,
		.hw_params = virtsnd_pcm_hw_params,
		.hw_free = virtsnd_pcm_hw_free,
		.prepare = virtsnd_pcm_prepare,
		.trigger = virtsnd_pcm_trigger,
		.sync_stop = virtsnd_pcm_sync_stop,
		.pointer = virtsnd_pcm_cp_pointer,
		.ack = virtsnd_pcm_cp_ack,
	},
};
