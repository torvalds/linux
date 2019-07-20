// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "capture.h"
#include "driver.h"
#include "playback.h"

/* impulse response volume controls */
static int snd_line6_impulse_volume_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_line6_impulse_volume_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = line6pcm->impulse_volume;
	return 0;
}

static int snd_line6_impulse_volume_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	int value = ucontrol->value.integer.value[0];
	int err;

	if (line6pcm->impulse_volume == value)
		return 0;

	line6pcm->impulse_volume = value;
	if (value > 0) {
		err = line6_pcm_acquire(line6pcm, LINE6_STREAM_IMPULSE, true);
		if (err < 0) {
			line6pcm->impulse_volume = 0;
			return err;
		}
	} else {
		line6_pcm_release(line6pcm, LINE6_STREAM_IMPULSE);
	}
	return 1;
}

/* impulse response period controls */
static int snd_line6_impulse_period_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 2000;
	return 0;
}

static int snd_line6_impulse_period_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = line6pcm->impulse_period;
	return 0;
}

static int snd_line6_impulse_period_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	int value = ucontrol->value.integer.value[0];

	if (line6pcm->impulse_period == value)
		return 0;

	line6pcm->impulse_period = value;
	return 1;
}

/*
	Unlink all currently active URBs.
*/
static void line6_unlink_audio_urbs(struct snd_line6_pcm *line6pcm,
				    struct line6_pcm_stream *pcms)
{
	int i;

	for (i = 0; i < line6pcm->line6->iso_buffers; i++) {
		if (test_bit(i, &pcms->active_urbs)) {
			if (!test_and_set_bit(i, &pcms->unlink_urbs))
				usb_unlink_urb(pcms->urbs[i]);
		}
	}
}

/*
	Wait until unlinking of all currently active URBs has been finished.
*/
static void line6_wait_clear_audio_urbs(struct snd_line6_pcm *line6pcm,
					struct line6_pcm_stream *pcms)
{
	int timeout = HZ;
	int i;
	int alive;

	do {
		alive = 0;
		for (i = 0; i < line6pcm->line6->iso_buffers; i++) {
			if (test_bit(i, &pcms->active_urbs))
				alive++;
		}
		if (!alive)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (--timeout > 0);
	if (alive)
		dev_err(line6pcm->line6->ifcdev,
			"timeout: still %d active urbs..\n", alive);
}

static inline struct line6_pcm_stream *
get_stream(struct snd_line6_pcm *line6pcm, int direction)
{
	return (direction == SNDRV_PCM_STREAM_PLAYBACK) ?
		&line6pcm->out : &line6pcm->in;
}

/* allocate a buffer if not opened yet;
 * call this in line6pcm.state_mutex
 */
static int line6_buffer_acquire(struct snd_line6_pcm *line6pcm,
				struct line6_pcm_stream *pstr, int direction, int type)
{
	const int pkt_size =
		(direction == SNDRV_PCM_STREAM_PLAYBACK) ?
			line6pcm->max_packet_size_out :
			line6pcm->max_packet_size_in;

	/* Invoked multiple times in a row so allocate once only */
	if (!test_and_set_bit(type, &pstr->opened) && !pstr->buffer) {
		pstr->buffer =
			kmalloc(array3_size(line6pcm->line6->iso_buffers,
					    LINE6_ISO_PACKETS, pkt_size),
				GFP_KERNEL);
		if (!pstr->buffer)
			return -ENOMEM;
	}
	return 0;
}

/* free a buffer if all streams are closed;
 * call this in line6pcm.state_mutex
 */
static void line6_buffer_release(struct snd_line6_pcm *line6pcm,
				 struct line6_pcm_stream *pstr, int type)
{
	clear_bit(type, &pstr->opened);
	if (!pstr->opened) {
		line6_wait_clear_audio_urbs(line6pcm, pstr);
		kfree(pstr->buffer);
		pstr->buffer = NULL;
	}
}

/* start a PCM stream */
static int line6_stream_start(struct snd_line6_pcm *line6pcm, int direction,
			      int type)
{
	unsigned long flags;
	struct line6_pcm_stream *pstr = get_stream(line6pcm, direction);
	int ret = 0;

	spin_lock_irqsave(&pstr->lock, flags);
	if (!test_and_set_bit(type, &pstr->running) &&
	    !(pstr->active_urbs || pstr->unlink_urbs)) {
		pstr->count = 0;
		/* Submit all currently available URBs */
		if (direction == SNDRV_PCM_STREAM_PLAYBACK)
			ret = line6_submit_audio_out_all_urbs(line6pcm);
		else
			ret = line6_submit_audio_in_all_urbs(line6pcm);
	}

	if (ret < 0)
		clear_bit(type, &pstr->running);
	spin_unlock_irqrestore(&pstr->lock, flags);
	return ret;
}

/* stop a PCM stream; this doesn't sync with the unlinked URBs */
static void line6_stream_stop(struct snd_line6_pcm *line6pcm, int direction,
			  int type)
{
	unsigned long flags;
	struct line6_pcm_stream *pstr = get_stream(line6pcm, direction);

	spin_lock_irqsave(&pstr->lock, flags);
	clear_bit(type, &pstr->running);
	if (!pstr->running) {
		spin_unlock_irqrestore(&pstr->lock, flags);
		line6_unlink_audio_urbs(line6pcm, pstr);
		spin_lock_irqsave(&pstr->lock, flags);
		if (direction == SNDRV_PCM_STREAM_CAPTURE) {
			line6pcm->prev_fbuf = NULL;
			line6pcm->prev_fsize = 0;
		}
	}
	spin_unlock_irqrestore(&pstr->lock, flags);
}

/* common PCM trigger callback */
int snd_line6_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	int err;

	clear_bit(LINE6_FLAG_PREPARED, &line6pcm->flags);

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;

		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
			if (s->stream == SNDRV_PCM_STREAM_CAPTURE &&
				(line6pcm->line6->properties->capabilities &
					LINE6_CAP_IN_NEEDS_OUT)) {
				err = line6_stream_start(line6pcm, SNDRV_PCM_STREAM_PLAYBACK,
						 LINE6_STREAM_CAPTURE_HELPER);
				if (err < 0)
					return err;
			}
			err = line6_stream_start(line6pcm, s->stream,
						 LINE6_STREAM_PCM);
			if (err < 0)
				return err;
			break;

		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
			if (s->stream == SNDRV_PCM_STREAM_CAPTURE &&
				(line6pcm->line6->properties->capabilities &
					LINE6_CAP_IN_NEEDS_OUT)) {
				line6_stream_stop(line6pcm, SNDRV_PCM_STREAM_PLAYBACK,
					  LINE6_STREAM_CAPTURE_HELPER);
			}
			line6_stream_stop(line6pcm, s->stream,
					  LINE6_STREAM_PCM);
			break;

		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			if (s->stream != SNDRV_PCM_STREAM_PLAYBACK)
				return -EINVAL;
			set_bit(LINE6_FLAG_PAUSE_PLAYBACK, &line6pcm->flags);
			break;

		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			if (s->stream != SNDRV_PCM_STREAM_PLAYBACK)
				return -EINVAL;
			clear_bit(LINE6_FLAG_PAUSE_PLAYBACK, &line6pcm->flags);
			break;

		default:
			return -EINVAL;
		}
	}

	return 0;
}

/* common PCM pointer callback */
snd_pcm_uframes_t snd_line6_pointer(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct line6_pcm_stream *pstr = get_stream(line6pcm, substream->stream);

	return pstr->pos_done;
}

/* Acquire and optionally start duplex streams:
 * type is either LINE6_STREAM_IMPULSE or LINE6_STREAM_MONITOR
 */
int line6_pcm_acquire(struct snd_line6_pcm *line6pcm, int type, bool start)
{
	struct line6_pcm_stream *pstr;
	int ret = 0, dir;

	/* TODO: We should assert SNDRV_PCM_STREAM_PLAYBACK/CAPTURE == 0/1 */
	mutex_lock(&line6pcm->state_mutex);
	for (dir = 0; dir < 2; dir++) {
		pstr = get_stream(line6pcm, dir);
		ret = line6_buffer_acquire(line6pcm, pstr, dir, type);
		if (ret < 0)
			goto error;
		if (!pstr->running)
			line6_wait_clear_audio_urbs(line6pcm, pstr);
	}
	if (start) {
		for (dir = 0; dir < 2; dir++) {
			ret = line6_stream_start(line6pcm, dir, type);
			if (ret < 0)
				goto error;
		}
	}
 error:
	mutex_unlock(&line6pcm->state_mutex);
	if (ret < 0)
		line6_pcm_release(line6pcm, type);
	return ret;
}
EXPORT_SYMBOL_GPL(line6_pcm_acquire);

/* Stop and release duplex streams */
void line6_pcm_release(struct snd_line6_pcm *line6pcm, int type)
{
	struct line6_pcm_stream *pstr;
	int dir;

	mutex_lock(&line6pcm->state_mutex);
	for (dir = 0; dir < 2; dir++)
		line6_stream_stop(line6pcm, dir, type);
	for (dir = 0; dir < 2; dir++) {
		pstr = get_stream(line6pcm, dir);
		line6_buffer_release(line6pcm, pstr, type);
	}
	mutex_unlock(&line6pcm->state_mutex);
}
EXPORT_SYMBOL_GPL(line6_pcm_release);

/* common PCM hw_params callback */
int snd_line6_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *hw_params)
{
	int ret;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct line6_pcm_stream *pstr = get_stream(line6pcm, substream->stream);

	mutex_lock(&line6pcm->state_mutex);
	ret = line6_buffer_acquire(line6pcm, pstr, substream->stream,
	                           LINE6_STREAM_PCM);
	if (ret < 0)
		goto error;

	ret = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (ret < 0) {
		line6_buffer_release(line6pcm, pstr, LINE6_STREAM_PCM);
		goto error;
	}

	pstr->period = params_period_bytes(hw_params);
 error:
	mutex_unlock(&line6pcm->state_mutex);
	return ret;
}

/* common PCM hw_free callback */
int snd_line6_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct line6_pcm_stream *pstr = get_stream(line6pcm, substream->stream);

	mutex_lock(&line6pcm->state_mutex);
	line6_buffer_release(line6pcm, pstr, LINE6_STREAM_PCM);
	mutex_unlock(&line6pcm->state_mutex);
	return snd_pcm_lib_free_pages(substream);
}


/* control info callback */
static int snd_line6_control_playback_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 256;
	return 0;
}

/* control get callback */
static int snd_line6_control_playback_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int i;
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	for (i = 0; i < 2; i++)
		ucontrol->value.integer.value[i] = line6pcm->volume_playback[i];

	return 0;
}

/* control put callback */
static int snd_line6_control_playback_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int i, changed = 0;
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	for (i = 0; i < 2; i++)
		if (line6pcm->volume_playback[i] !=
		    ucontrol->value.integer.value[i]) {
			line6pcm->volume_playback[i] =
			    ucontrol->value.integer.value[i];
			changed = 1;
		}

	return changed;
}

/* control definition */
static const struct snd_kcontrol_new line6_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Volume",
		.info = snd_line6_control_playback_info,
		.get = snd_line6_control_playback_get,
		.put = snd_line6_control_playback_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Impulse Response Volume",
		.info = snd_line6_impulse_volume_info,
		.get = snd_line6_impulse_volume_get,
		.put = snd_line6_impulse_volume_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Impulse Response Period",
		.info = snd_line6_impulse_period_info,
		.get = snd_line6_impulse_period_get,
		.put = snd_line6_impulse_period_put
	},
};

/*
	Cleanup the PCM device.
*/
static void cleanup_urbs(struct line6_pcm_stream *pcms, int iso_buffers)
{
	int i;

	/* Most likely impossible in current code... */
	if (pcms->urbs == NULL)
		return;

	for (i = 0; i < iso_buffers; i++) {
		if (pcms->urbs[i]) {
			usb_kill_urb(pcms->urbs[i]);
			usb_free_urb(pcms->urbs[i]);
		}
	}
	kfree(pcms->urbs);
	pcms->urbs = NULL;
}

static void line6_cleanup_pcm(struct snd_pcm *pcm)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_chip(pcm);

	cleanup_urbs(&line6pcm->out, line6pcm->line6->iso_buffers);
	cleanup_urbs(&line6pcm->in, line6pcm->line6->iso_buffers);
	kfree(line6pcm);
}

/* create a PCM device */
static int snd_line6_new_pcm(struct usb_line6 *line6, struct snd_pcm **pcm_ret)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(line6->card, (char *)line6->properties->name,
			  0, 1, 1, pcm_ret);
	if (err < 0)
		return err;
	pcm = *pcm_ret;
	strcpy(pcm->name, line6->properties->name);

	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_line6_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_line6_capture_ops);

	/* pre-allocation of buffers */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data
					      (GFP_KERNEL), 64 * 1024,
					      128 * 1024);
	return 0;
}

/*
	Sync with PCM stream stops.
*/
void line6_pcm_disconnect(struct snd_line6_pcm *line6pcm)
{
	line6_unlink_audio_urbs(line6pcm, &line6pcm->out);
	line6_unlink_audio_urbs(line6pcm, &line6pcm->in);
	line6_wait_clear_audio_urbs(line6pcm, &line6pcm->out);
	line6_wait_clear_audio_urbs(line6pcm, &line6pcm->in);
}

/*
	Create and register the PCM device and mixer entries.
	Create URBs for playback and capture.
*/
int line6_init_pcm(struct usb_line6 *line6,
		   struct line6_pcm_properties *properties)
{
	int i, err;
	unsigned ep_read = line6->properties->ep_audio_r;
	unsigned ep_write = line6->properties->ep_audio_w;
	struct snd_pcm *pcm;
	struct snd_line6_pcm *line6pcm;

	if (!(line6->properties->capabilities & LINE6_CAP_PCM))
		return 0;	/* skip PCM initialization and report success */

	err = snd_line6_new_pcm(line6, &pcm);
	if (err < 0)
		return err;

	line6pcm = kzalloc(sizeof(*line6pcm), GFP_KERNEL);
	if (!line6pcm)
		return -ENOMEM;

	mutex_init(&line6pcm->state_mutex);
	line6pcm->pcm = pcm;
	line6pcm->properties = properties;
	line6pcm->volume_playback[0] = line6pcm->volume_playback[1] = 255;
	line6pcm->volume_monitor = 255;
	line6pcm->line6 = line6;

	line6pcm->max_packet_size_in =
		usb_maxpacket(line6->usbdev,
			usb_rcvisocpipe(line6->usbdev, ep_read), 0);
	line6pcm->max_packet_size_out =
		usb_maxpacket(line6->usbdev,
			usb_sndisocpipe(line6->usbdev, ep_write), 1);
	if (!line6pcm->max_packet_size_in || !line6pcm->max_packet_size_out) {
		dev_err(line6pcm->line6->ifcdev,
			"cannot get proper max packet size\n");
		return -EINVAL;
	}

	spin_lock_init(&line6pcm->out.lock);
	spin_lock_init(&line6pcm->in.lock);
	line6pcm->impulse_period = LINE6_IMPULSE_DEFAULT_PERIOD;

	line6->line6pcm = line6pcm;

	pcm->private_data = line6pcm;
	pcm->private_free = line6_cleanup_pcm;

	err = line6_create_audio_out_urbs(line6pcm);
	if (err < 0)
		return err;

	err = line6_create_audio_in_urbs(line6pcm);
	if (err < 0)
		return err;

	/* mixer: */
	for (i = 0; i < ARRAY_SIZE(line6_controls); i++) {
		err = snd_ctl_add(line6->card,
				  snd_ctl_new1(&line6_controls[i], line6pcm));
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(line6_init_pcm);

/* prepare pcm callback */
int snd_line6_prepare(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct line6_pcm_stream *pstr = get_stream(line6pcm, substream->stream);

	mutex_lock(&line6pcm->state_mutex);
	if (!pstr->running)
		line6_wait_clear_audio_urbs(line6pcm, pstr);

	if (!test_and_set_bit(LINE6_FLAG_PREPARED, &line6pcm->flags)) {
		line6pcm->out.count = 0;
		line6pcm->out.pos = 0;
		line6pcm->out.pos_done = 0;
		line6pcm->out.bytes = 0;
		line6pcm->in.count = 0;
		line6pcm->in.pos_done = 0;
		line6pcm->in.bytes = 0;
	}

	mutex_unlock(&line6pcm->state_mutex);
	return 0;
}
