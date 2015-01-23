/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
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

	if (line6pcm->impulse_volume == value)
		return 0;

	line6pcm->impulse_volume = value;
	if (value > 0)
		line6_pcm_acquire(line6pcm, LINE6_BITS_PCM_IMPULSE);
	else
		line6_pcm_release(line6pcm, LINE6_BITS_PCM_IMPULSE);
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

	for (i = 0; i < LINE6_ISO_BUFFERS; i++) {
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
		for (i = 0; i < LINE6_ISO_BUFFERS; i++) {
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

static int line6_alloc_stream_buffer(struct snd_line6_pcm *line6pcm,
				     struct line6_pcm_stream *pcms)
{
	/* Invoked multiple times in a row so allocate once only */
	if (pcms->buffer)
		return 0;

	pcms->buffer = kmalloc(LINE6_ISO_BUFFERS * LINE6_ISO_PACKETS *
			       line6pcm->max_packet_size, GFP_KERNEL);
	if (!pcms->buffer)
		return -ENOMEM;
	return 0;
}

static void line6_free_stream_buffer(struct snd_line6_pcm *line6pcm,
				     struct line6_pcm_stream *pcms)
{
	kfree(pcms->buffer);
	pcms->buffer = NULL;
}

static bool test_flags(unsigned long flags0, unsigned long flags1,
		       unsigned long mask)
{
	return ((flags0 & mask) == 0) && ((flags1 & mask) != 0);
}

int line6_pcm_acquire(struct snd_line6_pcm *line6pcm, int channels)
{
	unsigned long flags_old, flags_new, flags_final;
	int err;

	do {
		flags_old = ACCESS_ONCE(line6pcm->flags);
		flags_new = flags_old | channels;
	} while (cmpxchg(&line6pcm->flags, flags_old, flags_new) != flags_old);

	flags_final = 0;

	line6pcm->prev_fbuf = NULL;

	if (test_flags(flags_old, flags_new, LINE6_BITS_CAPTURE_BUFFER)) {
		err = line6_alloc_stream_buffer(line6pcm, &line6pcm->in);
		if (err < 0)
			goto pcm_acquire_error;
		flags_final |= channels & LINE6_BITS_CAPTURE_BUFFER;
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_CAPTURE_STREAM)) {
		/*
		   Waiting for completion of active URBs in the stop handler is
		   a bug, we therefore report an error if capturing is restarted
		   too soon.
		 */
		if (line6pcm->in.active_urbs || line6pcm->in.unlink_urbs) {
			dev_err(line6pcm->line6->ifcdev, "Device not yet ready\n");
			err = -EBUSY;
			goto pcm_acquire_error;
		}

		line6pcm->in.count = 0;
		line6pcm->prev_fsize = 0;
		err = line6_submit_audio_in_all_urbs(line6pcm);

		if (err < 0)
			goto pcm_acquire_error;

		flags_final |= channels & LINE6_BITS_CAPTURE_STREAM;
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_PLAYBACK_BUFFER)) {
		err = line6_alloc_stream_buffer(line6pcm, &line6pcm->out);
		if (err < 0)
			goto pcm_acquire_error;
		flags_final |= channels & LINE6_BITS_PLAYBACK_BUFFER;
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_PLAYBACK_STREAM)) {
		/*
		  See comment above regarding PCM restart.
		*/
		if (line6pcm->out.active_urbs || line6pcm->out.unlink_urbs) {
			dev_err(line6pcm->line6->ifcdev, "Device not yet ready\n");
			return -EBUSY;
		}

		line6pcm->out.count = 0;
		err = line6_submit_audio_out_all_urbs(line6pcm);

		if (err < 0)
			goto pcm_acquire_error;

		flags_final |= channels & LINE6_BITS_PLAYBACK_STREAM;
	}

	return 0;

pcm_acquire_error:
	/*
	   If not all requested resources/streams could be obtained, release
	   those which were successfully obtained (if any).
	*/
	line6_pcm_release(line6pcm, flags_final);
	return err;
}
EXPORT_SYMBOL_GPL(line6_pcm_acquire);

int line6_pcm_release(struct snd_line6_pcm *line6pcm, int channels)
{
	unsigned long flags_old, flags_new;

	do {
		flags_old = ACCESS_ONCE(line6pcm->flags);
		flags_new = flags_old & ~channels;
	} while (cmpxchg(&line6pcm->flags, flags_old, flags_new) != flags_old);

	if (test_flags(flags_new, flags_old, LINE6_BITS_CAPTURE_STREAM))
		line6_unlink_audio_urbs(line6pcm, &line6pcm->in);

	if (test_flags(flags_new, flags_old, LINE6_BITS_CAPTURE_BUFFER)) {
		line6_wait_clear_audio_urbs(line6pcm, &line6pcm->in);
		line6_free_stream_buffer(line6pcm, &line6pcm->in);
	}

	if (test_flags(flags_new, flags_old, LINE6_BITS_PLAYBACK_STREAM))
		line6_unlink_audio_urbs(line6pcm, &line6pcm->out);

	if (test_flags(flags_new, flags_old, LINE6_BITS_PLAYBACK_BUFFER)) {
		line6_wait_clear_audio_urbs(line6pcm, &line6pcm->out);
		line6_free_stream_buffer(line6pcm, &line6pcm->out);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(line6_pcm_release);

/* trigger callback */
int snd_line6_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	int err = 0;

	clear_bit(LINE6_INDEX_PREPARED, &line6pcm->flags);

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;
		switch (s->stream) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			err = snd_line6_playback_trigger(line6pcm, cmd);
			break;

		case SNDRV_PCM_STREAM_CAPTURE:
			err = snd_line6_capture_trigger(line6pcm, cmd);
			break;

		default:
			dev_err(line6pcm->line6->ifcdev,
				"Unknown stream direction %d\n", s->stream);
			err = -EINVAL;
			break;
		}
		if (err < 0)
			break;
	}

	return err;
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
static struct snd_kcontrol_new line6_controls[] = {
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
static void cleanup_urbs(struct line6_pcm_stream *pcms)
{
	int i;

	for (i = 0; i < LINE6_ISO_BUFFERS; i++) {
		if (pcms->urbs[i]) {
			usb_kill_urb(pcms->urbs[i]);
			usb_free_urb(pcms->urbs[i]);
		}
	}
}

static void line6_cleanup_pcm(struct snd_pcm *pcm)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_chip(pcm);

	cleanup_urbs(&line6pcm->out);
	cleanup_urbs(&line6pcm->in);
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

	line6pcm->pcm = pcm;
	line6pcm->properties = properties;
	line6pcm->volume_playback[0] = line6pcm->volume_playback[1] = 255;
	line6pcm->volume_monitor = 255;
	line6pcm->line6 = line6;

	/* Read and write buffers are sized identically, so choose minimum */
	line6pcm->max_packet_size = min(
			usb_maxpacket(line6->usbdev,
				usb_rcvisocpipe(line6->usbdev, ep_read), 0),
			usb_maxpacket(line6->usbdev,
				usb_sndisocpipe(line6->usbdev, ep_write), 1));

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

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		if ((line6pcm->flags & LINE6_BITS_PLAYBACK_STREAM) == 0) {
			line6_unlink_audio_urbs(line6pcm, &line6pcm->out);
			line6_wait_clear_audio_urbs(line6pcm, &line6pcm->out);
		}
		break;

	case SNDRV_PCM_STREAM_CAPTURE:
		if ((line6pcm->flags & LINE6_BITS_CAPTURE_STREAM) == 0) {
			line6_unlink_audio_urbs(line6pcm, &line6pcm->in);
			line6_wait_clear_audio_urbs(line6pcm, &line6pcm->in);
		}
		break;
	}

	if (!test_and_set_bit(LINE6_INDEX_PREPARED, &line6pcm->flags)) {
		line6pcm->out.count = 0;
		line6pcm->out.pos = 0;
		line6pcm->out.pos_done = 0;
		line6pcm->out.bytes = 0;
		line6pcm->in.count = 0;
		line6pcm->in.pos_done = 0;
		line6pcm->in.bytes = 0;
	}

	return 0;
}
