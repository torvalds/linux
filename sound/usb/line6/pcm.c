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

	flags_final = flags_old;

	line6pcm->prev_fbuf = NULL;

	if (test_flags(flags_old, flags_new, LINE6_BITS_CAPTURE_BUFFER)) {
		/* Invoked multiple times in a row so allocate once only */
		if (!line6pcm->buffer_in) {
			line6pcm->buffer_in =
				kmalloc(LINE6_ISO_BUFFERS * LINE6_ISO_PACKETS *
					line6pcm->max_packet_size, GFP_KERNEL);
			if (!line6pcm->buffer_in) {
				err = -ENOMEM;
				goto pcm_acquire_error;
			}

			flags_final |= channels & LINE6_BITS_CAPTURE_BUFFER;
		}
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_CAPTURE_STREAM)) {
		/*
		   Waiting for completion of active URBs in the stop handler is
		   a bug, we therefore report an error if capturing is restarted
		   too soon.
		 */
		if (line6pcm->active_urb_in | line6pcm->unlink_urb_in) {
			dev_err(line6pcm->line6->ifcdev, "Device not yet ready\n");
			return -EBUSY;
		}

		line6pcm->count_in = 0;
		line6pcm->prev_fsize = 0;
		err = line6_submit_audio_in_all_urbs(line6pcm);

		if (err < 0)
			goto pcm_acquire_error;

		flags_final |= channels & LINE6_BITS_CAPTURE_STREAM;
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_PLAYBACK_BUFFER)) {
		/* Invoked multiple times in a row so allocate once only */
		if (!line6pcm->buffer_out) {
			line6pcm->buffer_out =
				kmalloc(LINE6_ISO_BUFFERS * LINE6_ISO_PACKETS *
					line6pcm->max_packet_size, GFP_KERNEL);
			if (!line6pcm->buffer_out) {
				err = -ENOMEM;
				goto pcm_acquire_error;
			}

			flags_final |= channels & LINE6_BITS_PLAYBACK_BUFFER;
		}
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_PLAYBACK_STREAM)) {
		/*
		  See comment above regarding PCM restart.
		*/
		if (line6pcm->active_urb_out | line6pcm->unlink_urb_out) {
			dev_err(line6pcm->line6->ifcdev, "Device not yet ready\n");
			return -EBUSY;
		}

		line6pcm->count_out = 0;
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
	line6_pcm_release(line6pcm, flags_final & channels);
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
		line6_unlink_audio_in_urbs(line6pcm);

	if (test_flags(flags_new, flags_old, LINE6_BITS_CAPTURE_BUFFER)) {
		line6_wait_clear_audio_in_urbs(line6pcm);
		line6_free_capture_buffer(line6pcm);
	}

	if (test_flags(flags_new, flags_old, LINE6_BITS_PLAYBACK_STREAM))
		line6_unlink_audio_out_urbs(line6pcm);

	if (test_flags(flags_new, flags_old, LINE6_BITS_PLAYBACK_BUFFER)) {
		line6_wait_clear_audio_out_urbs(line6pcm);
		line6_free_playback_buffer(line6pcm);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(line6_pcm_release);

/* trigger callback */
int snd_line6_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	int err;

	spin_lock(&line6pcm->lock_trigger);
	clear_bit(LINE6_INDEX_PREPARED, &line6pcm->flags);

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;
		switch (s->stream) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			err = snd_line6_playback_trigger(line6pcm, cmd);

			if (err < 0) {
				spin_unlock(&line6pcm->lock_trigger);
				return err;
			}

			break;

		case SNDRV_PCM_STREAM_CAPTURE:
			err = snd_line6_capture_trigger(line6pcm, cmd);

			if (err < 0) {
				spin_unlock(&line6pcm->lock_trigger);
				return err;
			}

			break;

		default:
			dev_err(line6pcm->line6->ifcdev,
				"Unknown stream direction %d\n", s->stream);
		}
	}

	spin_unlock(&line6pcm->lock_trigger);
	return 0;
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

	for (i = 2; i--;)
		ucontrol->value.integer.value[i] = line6pcm->volume_playback[i];

	return 0;
}

/* control put callback */
static int snd_line6_control_playback_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int i, changed = 0;
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	for (i = 2; i--;)
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
static void line6_cleanup_pcm(struct snd_pcm *pcm)
{
	int i;
	struct snd_line6_pcm *line6pcm = snd_pcm_chip(pcm);

	for (i = LINE6_ISO_BUFFERS; i--;) {
		if (line6pcm->urb_audio_out[i]) {
			usb_kill_urb(line6pcm->urb_audio_out[i]);
			usb_free_urb(line6pcm->urb_audio_out[i]);
		}
		if (line6pcm->urb_audio_in[i]) {
			usb_kill_urb(line6pcm->urb_audio_in[i]);
			usb_free_urb(line6pcm->urb_audio_in[i]);
		}
	}
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
	line6_unlink_wait_clear_audio_out_urbs(line6pcm);
	line6_unlink_wait_clear_audio_in_urbs(line6pcm);
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

	spin_lock_init(&line6pcm->lock_audio_out);
	spin_lock_init(&line6pcm->lock_audio_in);
	spin_lock_init(&line6pcm->lock_trigger);
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
		if ((line6pcm->flags & LINE6_BITS_PLAYBACK_STREAM) == 0)
			line6_unlink_wait_clear_audio_out_urbs(line6pcm);

		break;

	case SNDRV_PCM_STREAM_CAPTURE:
		if ((line6pcm->flags & LINE6_BITS_CAPTURE_STREAM) == 0)
			line6_unlink_wait_clear_audio_in_urbs(line6pcm);

		break;
	}

	if (!test_and_set_bit(LINE6_INDEX_PREPARED, &line6pcm->flags)) {
		line6pcm->count_out = 0;
		line6pcm->pos_out = 0;
		line6pcm->pos_out_done = 0;
		line6pcm->bytes_out = 0;
		line6pcm->count_in = 0;
		line6pcm->pos_in_done = 0;
		line6pcm->bytes_in = 0;
	}

	return 0;
}
