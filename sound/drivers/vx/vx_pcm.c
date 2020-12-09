// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram VX soundcards
 *
 * PCM part
 *
 * Copyright (c) 2002,2003 by Takashi Iwai <tiwai@suse.de>
 *
 * STRATEGY
 *  for playback, we send series of "chunks", which size is equal with the
 *  IBL size, typically 126 samples.  at each end of chunk, the end-of-buffer
 *  interrupt is notified, and the interrupt handler will feed the next chunk.
 *
 *  the current position is calculated from the sample count RMH.
 *  pipe->transferred is the counter of data which has been already transferred.
 *  if this counter reaches to the period size, snd_pcm_period_elapsed() will
 *  be issued.
 *
 *  for capture, the situation is much easier.
 *  to get a low latency response, we'll check the capture streams at each
 *  interrupt (capture stream has no EOB notification).  if the pending
 *  data is accumulated to the period size, snd_pcm_period_elapsed() is
 *  called and the pointer is updated.
 *
 *  the current point of read buffer is kept in pipe->hw_ptr.  note that
 *  this is in bytes.
 *
 * TODO
 *  - linked trigger for full-duplex mode.
 *  - scheduled action on the stream.
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include <sound/pcm.h>
#include <sound/vx_core.h>
#include "vx_cmd.h"


/*
 * read three pending pcm bytes via inb()
 */
static void vx_pcm_read_per_bytes(struct vx_core *chip, struct snd_pcm_runtime *runtime,
				  struct vx_pipe *pipe)
{
	int offset = pipe->hw_ptr;
	unsigned char *buf = (unsigned char *)(runtime->dma_area + offset);
	*buf++ = vx_inb(chip, RXH);
	if (++offset >= pipe->buffer_bytes) {
		offset = 0;
		buf = (unsigned char *)runtime->dma_area;
	}
	*buf++ = vx_inb(chip, RXM);
	if (++offset >= pipe->buffer_bytes) {
		offset = 0;
		buf = (unsigned char *)runtime->dma_area;
	}
	*buf++ = vx_inb(chip, RXL);
	if (++offset >= pipe->buffer_bytes) {
		offset = 0;
	}
	pipe->hw_ptr = offset;
}

/*
 * vx_set_pcx_time - convert from the PC time to the RMH status time.
 * @pc_time: the pointer for the PC-time to set
 * @dsp_time: the pointer for RMH status time array
 */
static void vx_set_pcx_time(struct vx_core *chip, pcx_time_t *pc_time,
			    unsigned int *dsp_time)
{
	dsp_time[0] = (unsigned int)((*pc_time) >> 24) & PCX_TIME_HI_MASK;
	dsp_time[1] = (unsigned int)(*pc_time) &  MASK_DSP_WORD;
}

/*
 * vx_set_differed_time - set the differed time if specified
 * @rmh: the rmh record to modify
 * @pipe: the pipe to be checked
 *
 * if the pipe is programmed with the differed time, set the DSP time
 * on the rmh and changes its command length.
 *
 * returns the increase of the command length.
 */
static int vx_set_differed_time(struct vx_core *chip, struct vx_rmh *rmh,
				struct vx_pipe *pipe)
{
	/* Update The length added to the RMH command by the timestamp */
	if (! (pipe->differed_type & DC_DIFFERED_DELAY))
		return 0;
		
	/* Set the T bit */
	rmh->Cmd[0] |= DSP_DIFFERED_COMMAND_MASK;

	/* Time stamp is the 1st following parameter */
	vx_set_pcx_time(chip, &pipe->pcx_time, &rmh->Cmd[1]);

	/* Add the flags to a notified differed command */
	if (pipe->differed_type & DC_NOTIFY_DELAY)
		rmh->Cmd[1] |= NOTIFY_MASK_TIME_HIGH ;

	/* Add the flags to a multiple differed command */
	if (pipe->differed_type & DC_MULTIPLE_DELAY)
		rmh->Cmd[1] |= MULTIPLE_MASK_TIME_HIGH;

	/* Add the flags to a stream-time differed command */
	if (pipe->differed_type & DC_STREAM_TIME_DELAY)
		rmh->Cmd[1] |= STREAM_MASK_TIME_HIGH;
		
	rmh->LgCmd += 2;
	return 2;
}

/*
 * vx_set_stream_format - send the stream format command
 * @pipe: the affected pipe
 * @data: format bitmask
 */
static int vx_set_stream_format(struct vx_core *chip, struct vx_pipe *pipe,
				unsigned int data)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, pipe->is_capture ?
		    CMD_FORMAT_STREAM_IN : CMD_FORMAT_STREAM_OUT);
	rmh.Cmd[0] |= pipe->number << FIELD_SIZE;

        /* Command might be longer since we may have to add a timestamp */
	vx_set_differed_time(chip, &rmh, pipe);

	rmh.Cmd[rmh.LgCmd] = (data & 0xFFFFFF00) >> 8;
	rmh.Cmd[rmh.LgCmd + 1] = (data & 0xFF) << 16 /*| (datal & 0xFFFF00) >> 8*/;
	rmh.LgCmd += 2;
    
	return vx_send_msg(chip, &rmh);
}


/*
 * vx_set_format - set the format of a pipe
 * @pipe: the affected pipe
 * @runtime: pcm runtime instance to be referred
 *
 * returns 0 if successful, or a negative error code.
 */
static int vx_set_format(struct vx_core *chip, struct vx_pipe *pipe,
			 struct snd_pcm_runtime *runtime)
{
	unsigned int header = HEADER_FMT_BASE;

	if (runtime->channels == 1)
		header |= HEADER_FMT_MONO;
	if (snd_pcm_format_little_endian(runtime->format))
		header |= HEADER_FMT_INTEL;
	if (runtime->rate < 32000 && runtime->rate > 11025)
		header |= HEADER_FMT_UPTO32;
	else if (runtime->rate <= 11025)
		header |= HEADER_FMT_UPTO11;

	switch (snd_pcm_format_physical_width(runtime->format)) {
	// case 8: break;
	case 16: header |= HEADER_FMT_16BITS; break;
	case 24: header |= HEADER_FMT_24BITS; break;
	default : 
		snd_BUG();
		return -EINVAL;
	}

	return vx_set_stream_format(chip, pipe, header);
}

/*
 * set / query the IBL size
 */
static int vx_set_ibl(struct vx_core *chip, struct vx_ibl_info *info)
{
	int err;
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_IBL);
	rmh.Cmd[0] |= info->size & 0x03ffff;
	err = vx_send_msg(chip, &rmh);
	if (err < 0)
		return err;
	info->size = rmh.Stat[0];
	info->max_size = rmh.Stat[1];
	info->min_size = rmh.Stat[2];
	info->granularity = rmh.Stat[3];
	snd_printdd(KERN_DEBUG "vx_set_ibl: size = %d, max = %d, min = %d, gran = %d\n",
		   info->size, info->max_size, info->min_size, info->granularity);
	return 0;
}


/*
 * vx_get_pipe_state - get the state of a pipe
 * @pipe: the pipe to be checked
 * @state: the pointer for the returned state
 *
 * checks the state of a given pipe, and stores the state (1 = running,
 * 0 = paused) on the given pointer.
 *
 * called from trigger callback only
 */
static int vx_get_pipe_state(struct vx_core *chip, struct vx_pipe *pipe, int *state)
{
	int err;
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_PIPE_STATE);
	vx_set_pipe_cmd_params(&rmh, pipe->is_capture, pipe->number, 0);
	err = vx_send_msg(chip, &rmh);
	if (! err)
		*state = (rmh.Stat[0] & (1 << pipe->number)) ? 1 : 0;
	return err;
}


/*
 * vx_query_hbuffer_size - query available h-buffer size in bytes
 * @pipe: the pipe to be checked
 *
 * return the available size on h-buffer in bytes,
 * or a negative error code.
 *
 * NOTE: calling this function always switches to the stream mode.
 *       you'll need to disconnect the host to get back to the
 *       normal mode.
 */
static int vx_query_hbuffer_size(struct vx_core *chip, struct vx_pipe *pipe)
{
	int result;
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_SIZE_HBUFFER);
	vx_set_pipe_cmd_params(&rmh, pipe->is_capture, pipe->number, 0);
	if (pipe->is_capture)
		rmh.Cmd[0] |= 0x00000001;
	result = vx_send_msg(chip, &rmh);
	if (! result)
		result = rmh.Stat[0] & 0xffff;
	return result;
}


/*
 * vx_pipe_can_start - query whether a pipe is ready for start
 * @pipe: the pipe to be checked
 *
 * return 1 if ready, 0 if not ready, and negative value on error.
 *
 * called from trigger callback only
 */
static int vx_pipe_can_start(struct vx_core *chip, struct vx_pipe *pipe)
{
	int err;
	struct vx_rmh rmh;
        
	vx_init_rmh(&rmh, CMD_CAN_START_PIPE);
	vx_set_pipe_cmd_params(&rmh, pipe->is_capture, pipe->number, 0);
	rmh.Cmd[0] |= 1;

	err = vx_send_msg(chip, &rmh);
	if (! err) {
		if (rmh.Stat[0])
			err = 1;
	}
	return err;
}

/*
 * vx_conf_pipe - tell the pipe to stand by and wait for IRQA.
 * @pipe: the pipe to be configured
 */
static int vx_conf_pipe(struct vx_core *chip, struct vx_pipe *pipe)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_CONF_PIPE);
	if (pipe->is_capture)
		rmh.Cmd[0] |= COMMAND_RECORD_MASK;
	rmh.Cmd[1] = 1 << pipe->number;
	return vx_send_msg(chip, &rmh);
}

/*
 * vx_send_irqa - trigger IRQA
 */
static int vx_send_irqa(struct vx_core *chip)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_SEND_IRQA);
	return vx_send_msg(chip, &rmh);
}


#define MAX_WAIT_FOR_DSP        250
/*
 * vx boards do not support inter-card sync, besides
 * only 126 samples require to be prepared before a pipe can start
 */
#define CAN_START_DELAY         2	/* wait 2ms only before asking if the pipe is ready*/
#define WAIT_STATE_DELAY        2	/* wait 2ms after irqA was requested and check if the pipe state toggled*/

/*
 * vx_toggle_pipe - start / pause a pipe
 * @pipe: the pipe to be triggered
 * @state: start = 1, pause = 0
 *
 * called from trigger callback only
 *
 */
static int vx_toggle_pipe(struct vx_core *chip, struct vx_pipe *pipe, int state)
{
	int err, i, cur_state;

	/* Check the pipe is not already in the requested state */
	if (vx_get_pipe_state(chip, pipe, &cur_state) < 0)
		return -EBADFD;
	if (state == cur_state)
		return 0;

	/* If a start is requested, ask the DSP to get prepared
	 * and wait for a positive acknowledge (when there are
	 * enough sound buffer for this pipe)
	 */
	if (state) {
		for (i = 0 ; i < MAX_WAIT_FOR_DSP; i++) {
			err = vx_pipe_can_start(chip, pipe);
			if (err > 0)
				break;
			/* Wait for a few, before asking again
			 * to avoid flooding the DSP with our requests
			 */
			mdelay(1);
		}
	}
    
	if ((err = vx_conf_pipe(chip, pipe)) < 0)
		return err;

	if ((err = vx_send_irqa(chip)) < 0)
		return err;
    
	/* If it completes successfully, wait for the pipes
	 * reaching the expected state before returning
	 * Check one pipe only (since they are synchronous)
	 */
	for (i = 0; i < MAX_WAIT_FOR_DSP; i++) {
		err = vx_get_pipe_state(chip, pipe, &cur_state);
		if (err < 0 || cur_state == state)
			break;
		err = -EIO;
		mdelay(1);
	}
	return err < 0 ? -EIO : 0;
}

    
/*
 * vx_stop_pipe - stop a pipe
 * @pipe: the pipe to be stopped
 *
 * called from trigger callback only
 */
static int vx_stop_pipe(struct vx_core *chip, struct vx_pipe *pipe)
{
	struct vx_rmh rmh;
	vx_init_rmh(&rmh, CMD_STOP_PIPE);
	vx_set_pipe_cmd_params(&rmh, pipe->is_capture, pipe->number, 0);
	return vx_send_msg(chip, &rmh);
}


/*
 * vx_alloc_pipe - allocate a pipe and initialize the pipe instance
 * @capture: 0 = playback, 1 = capture operation
 * @audioid: the audio id to be assigned
 * @num_audio: number of audio channels
 * @pipep: the returned pipe instance
 *
 * return 0 on success, or a negative error code.
 */
static int vx_alloc_pipe(struct vx_core *chip, int capture,
			 int audioid, int num_audio,
			 struct vx_pipe **pipep)
{
	int err;
	struct vx_pipe *pipe;
	struct vx_rmh rmh;
	int data_mode;

	*pipep = NULL;
	vx_init_rmh(&rmh, CMD_RES_PIPE);
	vx_set_pipe_cmd_params(&rmh, capture, audioid, num_audio);
#if 0	// NYI
	if (underrun_skip_sound)
		rmh.Cmd[0] |= BIT_SKIP_SOUND;
#endif	// NYI
	data_mode = (chip->uer_bits & IEC958_AES0_NONAUDIO) != 0;
	if (! capture && data_mode)
		rmh.Cmd[0] |= BIT_DATA_MODE;
	err = vx_send_msg(chip, &rmh);
	if (err < 0)
		return err;

	/* initialize the pipe record */
	pipe = kzalloc(sizeof(*pipe), GFP_KERNEL);
	if (! pipe) {
		/* release the pipe */
		vx_init_rmh(&rmh, CMD_FREE_PIPE);
		vx_set_pipe_cmd_params(&rmh, capture, audioid, 0);
		vx_send_msg(chip, &rmh);
		return -ENOMEM;
	}

	/* the pipe index should be identical with the audio index */
	pipe->number = audioid;
	pipe->is_capture = capture;
	pipe->channels = num_audio;
	pipe->differed_type = 0;
	pipe->pcx_time = 0;
	pipe->data_mode = data_mode;
	*pipep = pipe;

	return 0;
}


/*
 * vx_free_pipe - release a pipe
 * @pipe: pipe to be released
 */
static int vx_free_pipe(struct vx_core *chip, struct vx_pipe *pipe)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_FREE_PIPE);
	vx_set_pipe_cmd_params(&rmh, pipe->is_capture, pipe->number, 0);
	vx_send_msg(chip, &rmh);

	kfree(pipe);
	return 0;
}


/*
 * vx_start_stream - start the stream
 *
 * called from trigger callback only
 */
static int vx_start_stream(struct vx_core *chip, struct vx_pipe *pipe)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_START_ONE_STREAM);
	vx_set_stream_cmd_params(&rmh, pipe->is_capture, pipe->number);
	vx_set_differed_time(chip, &rmh, pipe);
	return vx_send_msg(chip, &rmh);
}


/*
 * vx_stop_stream - stop the stream
 *
 * called from trigger callback only
 */
static int vx_stop_stream(struct vx_core *chip, struct vx_pipe *pipe)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_STOP_STREAM);
	vx_set_stream_cmd_params(&rmh, pipe->is_capture, pipe->number);
	return vx_send_msg(chip, &rmh);
}


/*
 * playback hw information
 */

static const struct snd_pcm_hardware vx_pcm_playback_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_MMAP_VALID /*|*/
				 /*SNDRV_PCM_INFO_RESUME*/),
	.formats =		(/*SNDRV_PCM_FMTBIT_U8 |*/
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_3LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	126,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		VX_MAX_PERIODS,
	.fifo_size =		126,
};


/*
 * vx_pcm_playback_open - open callback for playback
 */
static int vx_pcm_playback_open(struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct vx_core *chip = snd_pcm_substream_chip(subs);
	struct vx_pipe *pipe = NULL;
	unsigned int audio;
	int err;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;

	audio = subs->pcm->device * 2;
	if (snd_BUG_ON(audio >= chip->audio_outs))
		return -EINVAL;
	
	/* playback pipe may have been already allocated for monitoring */
	pipe = chip->playback_pipes[audio];
	if (! pipe) {
		/* not allocated yet */
		err = vx_alloc_pipe(chip, 0, audio, 2, &pipe); /* stereo playback */
		if (err < 0)
			return err;
	}
	/* open for playback */
	pipe->references++;

	pipe->substream = subs;
	chip->playback_pipes[audio] = pipe;

	runtime->hw = vx_pcm_playback_hw;
	runtime->hw.period_bytes_min = chip->ibl.size;
	runtime->private_data = pipe;

	/* align to 4 bytes (otherwise will be problematic when 24bit is used) */ 
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 4);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 4);

	return 0;
}

/*
 * vx_pcm_playback_close - close callback for playback
 */
static int vx_pcm_playback_close(struct snd_pcm_substream *subs)
{
	struct vx_core *chip = snd_pcm_substream_chip(subs);
	struct vx_pipe *pipe;

	if (! subs->runtime->private_data)
		return -EINVAL;

	pipe = subs->runtime->private_data;

	if (--pipe->references == 0) {
		chip->playback_pipes[pipe->number] = NULL;
		vx_free_pipe(chip, pipe);
	}

	return 0;

}


/*
 * vx_notify_end_of_buffer - send "end-of-buffer" notifier at the given pipe
 * @pipe: the pipe to notify
 *
 * NB: call with a certain lock.
 */
static int vx_notify_end_of_buffer(struct vx_core *chip, struct vx_pipe *pipe)
{
	int err;
	struct vx_rmh rmh;  /* use a temporary rmh here */

	/* Toggle Dsp Host Interface into Message mode */
	vx_send_rih_nolock(chip, IRQ_PAUSE_START_CONNECT);
	vx_init_rmh(&rmh, CMD_NOTIFY_END_OF_BUFFER);
	vx_set_stream_cmd_params(&rmh, 0, pipe->number);
	err = vx_send_msg_nolock(chip, &rmh);
	if (err < 0)
		return err;
	/* Toggle Dsp Host Interface back to sound transfer mode */
	vx_send_rih_nolock(chip, IRQ_PAUSE_START_CONNECT);
	return 0;
}

/*
 * vx_pcm_playback_transfer_chunk - transfer a single chunk
 * @subs: substream
 * @pipe: the pipe to transfer
 * @size: chunk size in bytes
 *
 * transfer a single buffer chunk.  EOB notificaton is added after that.
 * called from the interrupt handler, too.
 *
 * return 0 if ok.
 */
static int vx_pcm_playback_transfer_chunk(struct vx_core *chip,
					  struct snd_pcm_runtime *runtime,
					  struct vx_pipe *pipe, int size)
{
	int space, err = 0;

	space = vx_query_hbuffer_size(chip, pipe);
	if (space < 0) {
		/* disconnect the host, SIZE_HBUF command always switches to the stream mode */
		vx_send_rih(chip, IRQ_CONNECT_STREAM_NEXT);
		snd_printd("error hbuffer\n");
		return space;
	}
	if (space < size) {
		vx_send_rih(chip, IRQ_CONNECT_STREAM_NEXT);
		snd_printd("no enough hbuffer space %d\n", space);
		return -EIO; /* XRUN */
	}
		
	/* we don't need irqsave here, because this function
	 * is called from either trigger callback or irq handler
	 */
	mutex_lock(&chip->lock);
	vx_pseudo_dma_write(chip, runtime, pipe, size);
	err = vx_notify_end_of_buffer(chip, pipe);
	/* disconnect the host, SIZE_HBUF command always switches to the stream mode */
	vx_send_rih_nolock(chip, IRQ_CONNECT_STREAM_NEXT);
	mutex_unlock(&chip->lock);
	return err;
}

/*
 * update the position of the given pipe.
 * pipe->position is updated and wrapped within the buffer size.
 * pipe->transferred is updated, too, but the size is not wrapped,
 * so that the caller can check the total transferred size later
 * (to call snd_pcm_period_elapsed).
 */
static int vx_update_pipe_position(struct vx_core *chip,
				   struct snd_pcm_runtime *runtime,
				   struct vx_pipe *pipe)
{
	struct vx_rmh rmh;
	int err, update;
	u64 count;

	vx_init_rmh(&rmh, CMD_STREAM_SAMPLE_COUNT);
	vx_set_pipe_cmd_params(&rmh, pipe->is_capture, pipe->number, 0);
	err = vx_send_msg(chip, &rmh);
	if (err < 0)
		return err;

	count = ((u64)(rmh.Stat[0] & 0xfffff) << 24) | (u64)rmh.Stat[1];
	update = (int)(count - pipe->cur_count);
	pipe->cur_count = count;
	pipe->position += update;
	if (pipe->position >= (int)runtime->buffer_size)
		pipe->position %= runtime->buffer_size;
	pipe->transferred += update;
	return 0;
}

/*
 * transfer the pending playback buffer data to DSP
 * called from interrupt handler
 */
static void vx_pcm_playback_transfer(struct vx_core *chip,
				     struct snd_pcm_substream *subs,
				     struct vx_pipe *pipe, int nchunks)
{
	int i, err;
	struct snd_pcm_runtime *runtime = subs->runtime;

	if (! pipe->prepared || (chip->chip_status & VX_STAT_IS_STALE))
		return;
	for (i = 0; i < nchunks; i++) {
		if ((err = vx_pcm_playback_transfer_chunk(chip, runtime, pipe,
							  chip->ibl.size)) < 0)
			return;
	}
}

/*
 * update the playback position and call snd_pcm_period_elapsed() if necessary
 * called from interrupt handler
 */
static void vx_pcm_playback_update(struct vx_core *chip,
				   struct snd_pcm_substream *subs,
				   struct vx_pipe *pipe)
{
	int err;
	struct snd_pcm_runtime *runtime = subs->runtime;

	if (pipe->running && ! (chip->chip_status & VX_STAT_IS_STALE)) {
		if ((err = vx_update_pipe_position(chip, runtime, pipe)) < 0)
			return;
		if (pipe->transferred >= (int)runtime->period_size) {
			pipe->transferred %= runtime->period_size;
			snd_pcm_period_elapsed(subs);
		}
	}
}

/*
 * vx_pcm_playback_trigger - trigger callback for playback
 */
static int vx_pcm_trigger(struct snd_pcm_substream *subs, int cmd)
{
	struct vx_core *chip = snd_pcm_substream_chip(subs);
	struct vx_pipe *pipe = subs->runtime->private_data;
	int err;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;
		
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (! pipe->is_capture)
			vx_pcm_playback_transfer(chip, subs, pipe, 2);
		err = vx_start_stream(chip, pipe);
		if (err < 0) {
			pr_debug("vx: cannot start stream\n");
			return err;
		}
		err = vx_toggle_pipe(chip, pipe, 1);
		if (err < 0) {
			pr_debug("vx: cannot start pipe\n");
			vx_stop_stream(chip, pipe);
			return err;
		}
		chip->pcm_running++;
		pipe->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		vx_toggle_pipe(chip, pipe, 0);
		vx_stop_pipe(chip, pipe);
		vx_stop_stream(chip, pipe);
		chip->pcm_running--;
		pipe->running = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if ((err = vx_toggle_pipe(chip, pipe, 0)) < 0)
			return err;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if ((err = vx_toggle_pipe(chip, pipe, 1)) < 0)
			return err;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * vx_pcm_playback_pointer - pointer callback for playback
 */
static snd_pcm_uframes_t vx_pcm_playback_pointer(struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct vx_pipe *pipe = runtime->private_data;
	return pipe->position;
}

/*
 * vx_pcm_prepare - prepare callback for playback and capture
 */
static int vx_pcm_prepare(struct snd_pcm_substream *subs)
{
	struct vx_core *chip = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct vx_pipe *pipe = runtime->private_data;
	int err, data_mode;
	// int max_size, nchunks;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;

	data_mode = (chip->uer_bits & IEC958_AES0_NONAUDIO) != 0;
	if (data_mode != pipe->data_mode && ! pipe->is_capture) {
		/* IEC958 status (raw-mode) was changed */
		/* we reopen the pipe */
		struct vx_rmh rmh;
		snd_printdd(KERN_DEBUG "reopen the pipe with data_mode = %d\n", data_mode);
		vx_init_rmh(&rmh, CMD_FREE_PIPE);
		vx_set_pipe_cmd_params(&rmh, 0, pipe->number, 0);
		if ((err = vx_send_msg(chip, &rmh)) < 0)
			return err;
		vx_init_rmh(&rmh, CMD_RES_PIPE);
		vx_set_pipe_cmd_params(&rmh, 0, pipe->number, pipe->channels);
		if (data_mode)
			rmh.Cmd[0] |= BIT_DATA_MODE;
		if ((err = vx_send_msg(chip, &rmh)) < 0)
			return err;
		pipe->data_mode = data_mode;
	}

	if (chip->pcm_running && chip->freq != runtime->rate) {
		snd_printk(KERN_ERR "vx: cannot set different clock %d "
			   "from the current %d\n", runtime->rate, chip->freq);
		return -EINVAL;
	}
	vx_set_clock(chip, runtime->rate);

	if ((err = vx_set_format(chip, pipe, runtime)) < 0)
		return err;

	if (vx_is_pcmcia(chip)) {
		pipe->align = 2; /* 16bit word */
	} else {
		pipe->align = 4; /* 32bit word */
	}

	pipe->buffer_bytes = frames_to_bytes(runtime, runtime->buffer_size);
	pipe->period_bytes = frames_to_bytes(runtime, runtime->period_size);
	pipe->hw_ptr = 0;

	/* set the timestamp */
	vx_update_pipe_position(chip, runtime, pipe);
	/* clear again */
	pipe->transferred = 0;
	pipe->position = 0;

	pipe->prepared = 1;

	return 0;
}


/*
 * operators for PCM playback
 */
static const struct snd_pcm_ops vx_pcm_playback_ops = {
	.open =		vx_pcm_playback_open,
	.close =	vx_pcm_playback_close,
	.prepare =	vx_pcm_prepare,
	.trigger =	vx_pcm_trigger,
	.pointer =	vx_pcm_playback_pointer,
};


/*
 * playback hw information
 */

static const struct snd_pcm_hardware vx_pcm_capture_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_MMAP_VALID /*|*/
				 /*SNDRV_PCM_INFO_RESUME*/),
	.formats =		(/*SNDRV_PCM_FMTBIT_U8 |*/
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_3LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	126,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		VX_MAX_PERIODS,
	.fifo_size =		126,
};


/*
 * vx_pcm_capture_open - open callback for capture
 */
static int vx_pcm_capture_open(struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct vx_core *chip = snd_pcm_substream_chip(subs);
	struct vx_pipe *pipe;
	struct vx_pipe *pipe_out_monitoring = NULL;
	unsigned int audio;
	int err;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;

	audio = subs->pcm->device * 2;
	if (snd_BUG_ON(audio >= chip->audio_ins))
		return -EINVAL;
	err = vx_alloc_pipe(chip, 1, audio, 2, &pipe);
	if (err < 0)
		return err;
	pipe->substream = subs;
	chip->capture_pipes[audio] = pipe;

	/* check if monitoring is needed */
	if (chip->audio_monitor_active[audio]) {
		pipe_out_monitoring = chip->playback_pipes[audio];
		if (! pipe_out_monitoring) {
			/* allocate a pipe */
			err = vx_alloc_pipe(chip, 0, audio, 2, &pipe_out_monitoring);
			if (err < 0)
				return err;
			chip->playback_pipes[audio] = pipe_out_monitoring;
		}
		pipe_out_monitoring->references++;
		/* 
		   if an output pipe is available, it's audios still may need to be 
		   unmuted. hence we'll have to call a mixer entry point.
		*/
		vx_set_monitor_level(chip, audio, chip->audio_monitor[audio],
				     chip->audio_monitor_active[audio]);
		/* assuming stereo */
		vx_set_monitor_level(chip, audio+1, chip->audio_monitor[audio+1],
				     chip->audio_monitor_active[audio+1]); 
	}

	pipe->monitoring_pipe = pipe_out_monitoring; /* default value NULL */

	runtime->hw = vx_pcm_capture_hw;
	runtime->hw.period_bytes_min = chip->ibl.size;
	runtime->private_data = pipe;

	/* align to 4 bytes (otherwise will be problematic when 24bit is used) */ 
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 4);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 4);

	return 0;
}

/*
 * vx_pcm_capture_close - close callback for capture
 */
static int vx_pcm_capture_close(struct snd_pcm_substream *subs)
{
	struct vx_core *chip = snd_pcm_substream_chip(subs);
	struct vx_pipe *pipe;
	struct vx_pipe *pipe_out_monitoring;
	
	if (! subs->runtime->private_data)
		return -EINVAL;
	pipe = subs->runtime->private_data;
	chip->capture_pipes[pipe->number] = NULL;

	pipe_out_monitoring = pipe->monitoring_pipe;

	/*
	  if an output pipe is attached to this input, 
	  check if it needs to be released.
	*/
	if (pipe_out_monitoring) {
		if (--pipe_out_monitoring->references == 0) {
			vx_free_pipe(chip, pipe_out_monitoring);
			chip->playback_pipes[pipe->number] = NULL;
			pipe->monitoring_pipe = NULL;
		}
	}
	
	vx_free_pipe(chip, pipe);
	return 0;
}



#define DMA_READ_ALIGN	6	/* hardware alignment for read */

/*
 * vx_pcm_capture_update - update the capture buffer
 */
static void vx_pcm_capture_update(struct vx_core *chip, struct snd_pcm_substream *subs,
				  struct vx_pipe *pipe)
{
	int size, space, count;
	struct snd_pcm_runtime *runtime = subs->runtime;

	if (!pipe->running || (chip->chip_status & VX_STAT_IS_STALE))
		return;

	size = runtime->buffer_size - snd_pcm_capture_avail(runtime);
	if (! size)
		return;
	size = frames_to_bytes(runtime, size);
	space = vx_query_hbuffer_size(chip, pipe);
	if (space < 0)
		goto _error;
	if (size > space)
		size = space;
	size = (size / 3) * 3; /* align to 3 bytes */
	if (size < DMA_READ_ALIGN)
		goto _error;

	/* keep the last 6 bytes, they will be read after disconnection */
	count = size - DMA_READ_ALIGN;
	/* read bytes until the current pointer reaches to the aligned position
	 * for word-transfer
	 */
	while (count > 0) {
		if ((pipe->hw_ptr % pipe->align) == 0)
			break;
		if (vx_wait_for_rx_full(chip) < 0)
			goto _error;
		vx_pcm_read_per_bytes(chip, runtime, pipe);
		count -= 3;
	}
	if (count > 0) {
		/* ok, let's accelerate! */
		int align = pipe->align * 3;
		space = (count / align) * align;
		if (space > 0) {
			vx_pseudo_dma_read(chip, runtime, pipe, space);
			count -= space;
		}
	}
	/* read the rest of bytes */
	while (count > 0) {
		if (vx_wait_for_rx_full(chip) < 0)
			goto _error;
		vx_pcm_read_per_bytes(chip, runtime, pipe);
		count -= 3;
	}
	/* disconnect the host, SIZE_HBUF command always switches to the stream mode */
	vx_send_rih(chip, IRQ_CONNECT_STREAM_NEXT);
	/* read the last pending 6 bytes */
	count = DMA_READ_ALIGN;
	while (count > 0) {
		vx_pcm_read_per_bytes(chip, runtime, pipe);
		count -= 3;
	}
	/* update the position */
	pipe->transferred += size;
	if (pipe->transferred >= pipe->period_bytes) {
		pipe->transferred %= pipe->period_bytes;
		snd_pcm_period_elapsed(subs);
	}
	return;

 _error:
	/* disconnect the host, SIZE_HBUF command always switches to the stream mode */
	vx_send_rih(chip, IRQ_CONNECT_STREAM_NEXT);
	return;
}

/*
 * vx_pcm_capture_pointer - pointer callback for capture
 */
static snd_pcm_uframes_t vx_pcm_capture_pointer(struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct vx_pipe *pipe = runtime->private_data;
	return bytes_to_frames(runtime, pipe->hw_ptr);
}

/*
 * operators for PCM capture
 */
static const struct snd_pcm_ops vx_pcm_capture_ops = {
	.open =		vx_pcm_capture_open,
	.close =	vx_pcm_capture_close,
	.prepare =	vx_pcm_prepare,
	.trigger =	vx_pcm_trigger,
	.pointer =	vx_pcm_capture_pointer,
};


/*
 * interrupt handler for pcm streams
 */
void vx_pcm_update_intr(struct vx_core *chip, unsigned int events)
{
	unsigned int i;
	struct vx_pipe *pipe;

#define EVENT_MASK	(END_OF_BUFFER_EVENTS_PENDING|ASYNC_EVENTS_PENDING)

	if (events & EVENT_MASK) {
		vx_init_rmh(&chip->irq_rmh, CMD_ASYNC);
		if (events & ASYNC_EVENTS_PENDING)
			chip->irq_rmh.Cmd[0] |= 0x00000001;	/* SEL_ASYNC_EVENTS */
		if (events & END_OF_BUFFER_EVENTS_PENDING)
			chip->irq_rmh.Cmd[0] |= 0x00000002;	/* SEL_END_OF_BUF_EVENTS */

		if (vx_send_msg(chip, &chip->irq_rmh) < 0) {
			snd_printdd(KERN_ERR "msg send error!!\n");
			return;
		}

		i = 1;
		while (i < chip->irq_rmh.LgStat) {
			int p, buf, capture, eob;
			p = chip->irq_rmh.Stat[i] & MASK_FIRST_FIELD;
			capture = (chip->irq_rmh.Stat[i] & 0x400000) ? 1 : 0;
			eob = (chip->irq_rmh.Stat[i] & 0x800000) ? 1 : 0;
			i++;
			if (events & ASYNC_EVENTS_PENDING)
				i++;
			buf = 1; /* force to transfer */
			if (events & END_OF_BUFFER_EVENTS_PENDING) {
				if (eob)
					buf = chip->irq_rmh.Stat[i];
				i++;
			}
			if (capture)
				continue;
			if (snd_BUG_ON(p < 0 || p >= chip->audio_outs))
				continue;
			pipe = chip->playback_pipes[p];
			if (pipe && pipe->substream) {
				vx_pcm_playback_update(chip, pipe->substream, pipe);
				vx_pcm_playback_transfer(chip, pipe->substream, pipe, buf);
			}
		}
	}

	/* update the capture pcm pointers as frequently as possible */
	for (i = 0; i < chip->audio_ins; i++) {
		pipe = chip->capture_pipes[i];
		if (pipe && pipe->substream)
			vx_pcm_capture_update(chip, pipe->substream, pipe);
	}
}


/*
 * vx_init_audio_io - check the available audio i/o and allocate pipe arrays
 */
static int vx_init_audio_io(struct vx_core *chip)
{
	struct vx_rmh rmh;
	int preferred;

	vx_init_rmh(&rmh, CMD_SUPPORTED);
	if (vx_send_msg(chip, &rmh) < 0) {
		snd_printk(KERN_ERR "vx: cannot get the supported audio data\n");
		return -ENXIO;
	}

	chip->audio_outs = rmh.Stat[0] & MASK_FIRST_FIELD;
	chip->audio_ins = (rmh.Stat[0] >> (FIELD_SIZE*2)) & MASK_FIRST_FIELD;
	chip->audio_info = rmh.Stat[1];

	/* allocate pipes */
	chip->playback_pipes = kcalloc(chip->audio_outs, sizeof(struct vx_pipe *), GFP_KERNEL);
	if (!chip->playback_pipes)
		return -ENOMEM;
	chip->capture_pipes = kcalloc(chip->audio_ins, sizeof(struct vx_pipe *), GFP_KERNEL);
	if (!chip->capture_pipes) {
		kfree(chip->playback_pipes);
		return -ENOMEM;
	}

	preferred = chip->ibl.size;
	chip->ibl.size = 0;
	vx_set_ibl(chip, &chip->ibl); /* query the info */
	if (preferred > 0) {
		chip->ibl.size = ((preferred + chip->ibl.granularity - 1) /
				  chip->ibl.granularity) * chip->ibl.granularity;
		if (chip->ibl.size > chip->ibl.max_size)
			chip->ibl.size = chip->ibl.max_size;
	} else
		chip->ibl.size = chip->ibl.min_size; /* set to the minimum */
	vx_set_ibl(chip, &chip->ibl);

	return 0;
}


/*
 * free callback for pcm
 */
static void snd_vx_pcm_free(struct snd_pcm *pcm)
{
	struct vx_core *chip = pcm->private_data;
	chip->pcm[pcm->device] = NULL;
	kfree(chip->playback_pipes);
	chip->playback_pipes = NULL;
	kfree(chip->capture_pipes);
	chip->capture_pipes = NULL;
}

/*
 * snd_vx_pcm_new - create and initialize a pcm
 */
int snd_vx_pcm_new(struct vx_core *chip)
{
	struct snd_pcm *pcm;
	unsigned int i;
	int err;

	if ((err = vx_init_audio_io(chip)) < 0)
		return err;

	for (i = 0; i < chip->hw->num_codecs; i++) {
		unsigned int outs, ins;
		outs = chip->audio_outs > i * 2 ? 1 : 0;
		ins = chip->audio_ins > i * 2 ? 1 : 0;
		if (! outs && ! ins)
			break;
		err = snd_pcm_new(chip->card, "VX PCM", i,
				  outs, ins, &pcm);
		if (err < 0)
			return err;
		if (outs)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &vx_pcm_playback_ops);
		if (ins)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &vx_pcm_capture_ops);
		snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC,
					       snd_dma_continuous_data(GFP_KERNEL | GFP_DMA32),
					       0, 0);

		pcm->private_data = chip;
		pcm->private_free = snd_vx_pcm_free;
		pcm->info_flags = 0;
		pcm->nonatomic = true;
		strcpy(pcm->name, chip->card->shortname);
		chip->pcm[i] = pcm;
	}

	return 0;
}
