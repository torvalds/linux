/*
 *   intel_mid_hdmi_audio_if.c - Intel HDMI audio driver for MID
 *
 *  Copyright (C) 2010 Intel Corp
 *  Authors:	Sailaja Bandarupalli <sailaja.bandarupalli@intel.com>
 *		Ramesh Babu K V <ramesh.babu@intel.com>
 *		Vaibhav Agarwal <vaibhav.agarwal@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * ALSA driver for Intel MID HDMI audio controller.  This file contains
 * interface functions exposed to HDMI Display driver and code to register
 * with ALSA framework..
 */

#define pr_fmt(fmt)		"had: " fmt

#include <linux/io.h>
#include <linux/jiffies.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include "intel_mid_hdmi_audio.h"


/**
 * hdmi_audio_query - hdmi audio query function
 *
 *@haddata: pointer to HAD private data
 *@event: audio event for which this method is invoked
 *
 * This function is called by client driver to query the
 * hdmi audio.
 */
int hdmi_audio_query(void *haddata, hdmi_audio_event_t event)
{
	struct snd_pcm_substream *substream = NULL;
	struct had_pvt_data *had_stream;
	unsigned long flag_irqs;
	struct snd_intelhad *intelhaddata = (struct snd_intelhad *)haddata;

	if (intelhaddata->stream_info.had_substream)
		substream = intelhaddata->stream_info.had_substream;
	had_stream = intelhaddata->private_data;
	switch (event.type) {
	case HAD_EVENT_QUERY_IS_AUDIO_BUSY:
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);

		if ((had_stream->stream_type == HAD_RUNNING_STREAM) ||
			substream) {
			spin_unlock_irqrestore(&intelhaddata->had_spinlock,
						flag_irqs);
			pr_debug("Audio stream active\n");
			return -EBUSY;
		}
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	break;

	case HAD_EVENT_QUERY_IS_AUDIO_SUSPENDED:
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
		if (intelhaddata->drv_status == HAD_DRV_SUSPENDED) {
			spin_unlock_irqrestore(&intelhaddata->had_spinlock,
						flag_irqs);
			pr_debug("Audio is suspended\n");
			return 1;
		}
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	break;

	default:
		pr_debug("error un-handled event !!\n");
		return -EINVAL;
	break;

	}

	return 0;
}

/**
 * hdmi_audio_suspend - power management suspend function
 *
 *@haddata: pointer to HAD private data
 *@event: pm event for which this method is invoked
 *
 * This function is called by client driver to suspend the
 * hdmi audio.
 */
int hdmi_audio_suspend(void *haddata, hdmi_audio_event_t event)
{
	int caps, retval = 0;
	struct had_pvt_data *had_stream;
	unsigned long flag_irqs;
	struct snd_pcm_substream *substream;
	struct snd_intelhad *intelhaddata = (struct snd_intelhad *)haddata;

	pr_debug("Enter:%s", __func__);

	had_stream = intelhaddata->private_data;
	substream = intelhaddata->stream_info.had_substream;

	if (intelhaddata->dev->power.runtime_status != RPM_SUSPENDED) {
		pr_err("audio stream is active\n");
		return -EAGAIN;
	}


	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		pr_debug("had not connected\n");
		return retval;
	}

	if (intelhaddata->drv_status == HAD_DRV_SUSPENDED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		pr_debug("had already suspended\n");
		return retval;
	}

	intelhaddata->drv_status = HAD_DRV_SUSPENDED;
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	/*
	 * ToDo: Need to disable UNDERRUN interrupts as well
	 *  caps = HDMI_AUDIO_UNDERRUN | HDMI_AUDIO_BUFFER_DONE;
	 */
	caps = HDMI_AUDIO_BUFFER_DONE;
	had_set_caps(HAD_SET_DISABLE_AUDIO_INT, &caps);
	had_set_caps(HAD_SET_DISABLE_AUDIO, NULL);
	pr_debug("Exit:%s", __func__);
	return retval;
}

/**
 * hdmi_audio_resume - power management resume function
 *
 *@haddata: pointer to HAD private data
 *
 * This function is called by client driver to resume the
 * hdmi audio.
 */
int hdmi_audio_resume(void *haddata)
{
	int caps, retval = 0;
	struct snd_intelhad *intelhaddata = (struct snd_intelhad *)haddata;
	unsigned long flag_irqs;

	pr_debug("Enter:%s", __func__);

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		pr_debug("had not connected\n");
		return 0;
	}

	if (intelhaddata->drv_status != HAD_DRV_SUSPENDED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		pr_err("had is not in suspended state\n");
		return 0;
	}

	if (had_get_hwstate(intelhaddata)) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		pr_err("Failed to resume. Device not accessible\n");
		return -ENODEV;
	}

	intelhaddata->drv_status = HAD_DRV_CONNECTED;
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	/*
	 * ToDo: Need to enable UNDERRUN interrupts as well
	 * caps = HDMI_AUDIO_UNDERRUN | HDMI_AUDIO_BUFFER_DONE;
	 */
	caps = HDMI_AUDIO_BUFFER_DONE;
	retval = had_set_caps(HAD_SET_ENABLE_AUDIO_INT, &caps);
	retval = had_set_caps(HAD_SET_ENABLE_AUDIO, NULL);
	pr_debug("Exit:%s", __func__);
	return retval;
}

static inline int had_chk_intrmiss(struct snd_intelhad *intelhaddata,
		enum intel_had_aud_buf_type buf_id)
{
	int i, intr_count = 0;
	enum intel_had_aud_buf_type buff_done;
	u32 buf_size, buf_addr;
	struct had_pvt_data *had_stream;
	unsigned long flag_irqs;

	had_stream = intelhaddata->private_data;

	buff_done = buf_id;

	intr_count = snd_intelhad_read_len(intelhaddata);
	if (intr_count > 1) {
		/* In case of active playback */
		pr_err("Driver detected %d missed buffer done interrupt(s)!!!!\n",
				(intr_count - 1));
		if (intr_count > 3)
			return intr_count;

		buf_id += (intr_count - 1);
		/* Reprogram registers*/
		for (i = buff_done; i < buf_id; i++) {
			int j = i % 4;

			buf_size = intelhaddata->buf_info[j].buf_size;
			buf_addr = intelhaddata->buf_info[j].buf_addr;
			had_write_register(AUD_BUF_A_LENGTH +
					(j * HAD_REG_WIDTH), buf_size);
			had_write_register(
					AUD_BUF_A_ADDR+(j * HAD_REG_WIDTH),
					(buf_addr | BIT(0) | BIT(1)));
		}
		buf_id = buf_id % 4;
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
		intelhaddata->buff_done = buf_id;
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	}

	return intr_count;
}

int had_process_buffer_done(struct snd_intelhad *intelhaddata)
{
	int retval = 0;
	u32 len = 1;
	enum intel_had_aud_buf_type buf_id;
	enum intel_had_aud_buf_type buff_done;
	struct pcm_stream_info *stream;
	u32 buf_size;
	struct had_pvt_data *had_stream;
	int intr_count;
	enum had_status_stream		stream_type;
	unsigned long flag_irqs;

	had_stream = intelhaddata->private_data;
	stream = &intelhaddata->stream_info;
	intr_count = 1;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		pr_err("%s:Device already disconnected\n", __func__);
		return retval;
	}
	buf_id = intelhaddata->curr_buf;
	intelhaddata->buff_done = buf_id;
	buff_done = intelhaddata->buff_done;
	buf_size = intelhaddata->buf_info[buf_id].buf_size;
	stream_type = had_stream->stream_type;

	pr_debug("Enter:%s buf_id=%d", __func__, buf_id);

	/* Every debug statement has an implication
	 * of ~5msec. Thus, avoid having >3 debug statements
	 * for each buffer_done handling.
	 */

	/* Check for any intr_miss in case of active playback */
	if (had_stream->stream_type == HAD_RUNNING_STREAM) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		intr_count = had_chk_intrmiss(intelhaddata, buf_id);
		if (!intr_count || (intr_count > 3)) {
			pr_err("HAD SW state in non-recoverable!!! mode\n");
			pr_err("Already played stale data\n");
			return retval;
		}
		buf_id += (intr_count - 1);
		buf_id = buf_id % 4;
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	}

	intelhaddata->buf_info[buf_id].is_valid = true;
	if (intelhaddata->valid_buf_cnt-1 == buf_id) {
		if (had_stream->stream_type >= HAD_RUNNING_STREAM)
			intelhaddata->curr_buf = HAD_BUF_TYPE_A;
	} else
		intelhaddata->curr_buf = buf_id + 1;

	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);

	if (had_get_hwstate(intelhaddata)) {
		pr_err("HDMI cable plugged-out\n");
		return retval;
	}

	/*Reprogram the registers with addr and length*/
	had_write_register(AUD_BUF_A_LENGTH +
			(buf_id * HAD_REG_WIDTH), buf_size);
	had_write_register(AUD_BUF_A_ADDR+(buf_id * HAD_REG_WIDTH),
			intelhaddata->buf_info[buf_id].buf_addr|
			BIT(0) | BIT(1));

	had_read_register(AUD_BUF_A_LENGTH + (buf_id * HAD_REG_WIDTH),
					&len);
	pr_debug("%s:Enabled buf[%d]\n", __func__, buf_id);

	/* In case of actual data,
	 * report buffer_done to above ALSA layer
	 */
	buf_size =  intelhaddata->buf_info[buf_id].buf_size;
	if (stream_type >= HAD_RUNNING_STREAM) {
		intelhaddata->stream_info.buffer_rendered +=
			(intr_count * buf_size);
		stream->period_elapsed(stream->had_substream);
	}

	return retval;
}

int had_process_buffer_underrun(struct snd_intelhad *intelhaddata)
{
	int retval = 0;
	enum intel_had_aud_buf_type buf_id;
	struct pcm_stream_info *stream;
	struct had_pvt_data *had_stream;
	enum had_status_stream stream_type;
	unsigned long flag_irqs;
	int drv_status;

	had_stream = intelhaddata->private_data;
	stream = &intelhaddata->stream_info;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	buf_id = intelhaddata->curr_buf;
	stream_type = had_stream->stream_type;
	intelhaddata->buff_done = buf_id;
	drv_status = intelhaddata->drv_status;
	if (stream_type == HAD_RUNNING_STREAM)
		intelhaddata->curr_buf = HAD_BUF_TYPE_A;

	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);

	pr_debug("Enter:%s buf_id=%d, stream_type=%d\n",
			__func__, buf_id, stream_type);

	intelhaddata->ops->handle_underrun(intelhaddata);

	if (drv_status == HAD_DRV_DISCONNECTED) {
		pr_err("%s:Device already disconnected\n", __func__);
		return retval;
	}

	if (stream_type == HAD_RUNNING_STREAM) {
		/* Report UNDERRUN error to above layers */
		intelhaddata->flag_underrun = 1;
		stream->period_elapsed(stream->had_substream);
	}

	return retval;
}

int had_process_hot_plug(struct snd_intelhad *intelhaddata)
{
	int retval = 0;
	enum intel_had_aud_buf_type buf_id;
	struct snd_pcm_substream *substream;
	struct had_pvt_data *had_stream;
	unsigned long flag_irqs;

	pr_debug("Enter:%s", __func__);

	substream = intelhaddata->stream_info.had_substream;
	had_stream = intelhaddata->private_data;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_CONNECTED) {
		pr_debug("Device already connected\n");
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		return retval;
	}
	buf_id = intelhaddata->curr_buf;
	intelhaddata->buff_done = buf_id;
	intelhaddata->drv_status = HAD_DRV_CONNECTED;
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);

	pr_debug("Processing HOT_PLUG, buf_id = %d\n", buf_id);

	 /* Query display driver for audio register base */
	if (intelhaddata->reg_ops.hdmi_audio_get_register_base
		(&intelhaddata->audio_reg_base)) {
			pr_err("Unable to get audio reg base from Display driver\n");
			goto err;
	}
	if(intelhaddata->audio_reg_base == 0){
		pr_err("audio reg base value is NULL\n");
		goto err;
	}

	pr_debug("%s audio_reg_base = %x\n",__func__, intelhaddata->audio_reg_base);

	/* Safety check */
	if (substream) {
		pr_debug("There should not be active PB from ALSA\n");
		pr_debug("Signifies, cable is plugged-in even before\n");
		pr_debug("processing snd_pcm_disconnect\n");
		/* Set runtime->state to hw_params done */
		snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
	}

	had_build_channel_allocation_map(intelhaddata);

	return retval;

err:
	pm_runtime_disable(intelhaddata->dev);
	intelhaddata->dev = NULL;
	return retval;
}

int had_process_hot_unplug(struct snd_intelhad *intelhaddata)
{
	int caps, retval = 0;
	enum intel_had_aud_buf_type buf_id;
	struct had_pvt_data *had_stream;
	unsigned long flag_irqs;

	pr_debug("Enter:%s", __func__);

	had_stream = intelhaddata->private_data;
	buf_id = intelhaddata->curr_buf;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		pr_debug("Device already disconnected\n");
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		return retval;
	} else {
		/* Disable Audio */
		caps = HDMI_AUDIO_BUFFER_DONE;
		retval = had_set_caps(HAD_SET_DISABLE_AUDIO_INT, &caps);
		retval = had_set_caps(HAD_SET_DISABLE_AUDIO, NULL);
		intelhaddata->ops->enable_audio(intelhaddata->stream_info.had_substream, 0);
	}

	intelhaddata->drv_status = HAD_DRV_DISCONNECTED;
	/* Report to above ALSA layer */
	if (intelhaddata->stream_info.had_substream != NULL) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		pr_debug("%s: unlock -> sending pcm_stop -> lock\n", __func__);
		snd_pcm_stop(intelhaddata->stream_info.had_substream,
				SNDRV_PCM_STATE_DISCONNECTED);
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	}

	had_stream->stream_type = HAD_INIT;
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	kfree(intelhaddata->chmap->chmap);
	intelhaddata->chmap->chmap = NULL;
	intelhaddata->audio_reg_base = 0;
	pr_debug("%s: unlocked -> returned\n", __func__);

	return retval;
}

/**
 * had_event_handler - Call back function to handle events
 *
 * @event_type: Event type to handle
 * @data: data related to the event_type
 *
 * This function is invoked to handle HDMI events from client driver.
 */
int had_event_handler(enum had_event_type event_type, void *data)
{
	int retval = 0;
	struct snd_intelhad *intelhaddata = data;
	enum intel_had_aud_buf_type buf_id;
	struct snd_pcm_substream *substream;
	struct had_pvt_data *had_stream;
	unsigned long flag_irqs;

	buf_id = intelhaddata->curr_buf;
	had_stream = intelhaddata->private_data;

	/* Switching to a function can drop atomicity even in INTR context.
	 * Thus, a big lock is acquired to maintain atomicity.
	 * This can be optimized later.
	 * Currently, only buffer_done/_underrun executes in INTR context.
	 * Also, locking is implemented separately to avoid real contention
	 * of data(struct intelhaddata) between IRQ/SOFT_IRQ/PROCESS context.
	 */
	substream = intelhaddata->stream_info.had_substream;
	switch (event_type) {
	case HAD_EVENT_AUDIO_BUFFER_DONE:
		retval = had_process_buffer_done(intelhaddata);
	break;

	case HAD_EVENT_AUDIO_BUFFER_UNDERRUN:
		retval = had_process_buffer_underrun(intelhaddata);
	break;

	case HAD_EVENT_HOT_PLUG:
		retval = had_process_hot_plug(intelhaddata);
	break;

	case HAD_EVENT_HOT_UNPLUG:
		retval = had_process_hot_unplug(intelhaddata);
	break;

	case HAD_EVENT_MODE_CHANGING:
		pr_debug(" called _event_handler with _MODE_CHANGE event\n");
		/* Process only if stream is active & cable Plugged-in */
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
		if (intelhaddata->drv_status >= HAD_DRV_DISCONNECTED) {
			spin_unlock_irqrestore(&intelhaddata->had_spinlock,
					flag_irqs);
			break;
		}
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		if ((had_stream->stream_type == HAD_RUNNING_STREAM)
				&& substream)
			retval = hdmi_audio_mode_change(substream);
	break;

	default:
		pr_debug("error un-handled event !!\n");
		retval = -EINVAL;
	break;

	}
	return retval;
}
