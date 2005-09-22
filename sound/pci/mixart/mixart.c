/*
 * Driver for Digigram miXart soundcards
 *
 * main file with alsa callbacks
 *
 * Copyright (c) 2003 by Digigram <alsa@digigram.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


#include <sound/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "mixart.h"
#include "mixart_hwdep.h"
#include "mixart_core.h"
#include "mixart_mixer.h"

#define CARD_NAME "miXart"

MODULE_AUTHOR("Digigram <alsa@digigram.com>");
MODULE_DESCRIPTION("Digigram " CARD_NAME);
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Digigram," CARD_NAME "}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;             /* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;              /* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;     /* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Digigram " CARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Digigram " CARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Digigram " CARD_NAME " soundcard.");

/*
 */

static struct pci_device_id snd_mixart_ids[] = {
	{ 0x1057, 0x0003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* MC8240 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_mixart_ids);


static int mixart_set_pipe_state(mixart_mgr_t *mgr, mixart_pipe_t* pipe, int start)
{
	mixart_group_state_req_t group_state;
	mixart_group_state_resp_t group_state_resp;
	mixart_msg_t request;
	int err;
	u32 system_msg_uid;

	switch(pipe->status) {
	case PIPE_RUNNING:
	case PIPE_CLOCK_SET:
		if(start) return 0; /* already started */
		break;
	case PIPE_STOPPED:
		if(!start) return 0; /* already stopped */
		break;
	default:
		snd_printk(KERN_ERR "error mixart_set_pipe_state called with wrong pipe->status!\n");
		return -EINVAL;      /* function called with wrong pipe status */
	}

	system_msg_uid = 0x12345678; /* the event ! (take care: the MSB and two LSB's have to be 0) */

	/* wait on the last MSG_SYSTEM_SEND_SYNCHRO_CMD command to be really finished */

	request.message_id = MSG_SYSTEM_WAIT_SYNCHRO_CMD;
	request.uid = (mixart_uid_t){0,0};
	request.data = &system_msg_uid;
	request.size = sizeof(system_msg_uid);

	err = snd_mixart_send_msg_wait_notif(mgr, &request, system_msg_uid);
	if(err) {
		snd_printk(KERN_ERR "error : MSG_SYSTEM_WAIT_SYNCHRO_CMD was not notified !\n");
		return err;
	}

	/* start or stop the pipe (1 pipe) */

	memset(&group_state, 0, sizeof(group_state));
	group_state.pipe_count = 1;
	group_state.pipe_uid[0] = pipe->group_uid;

	if(start)
		request.message_id = MSG_STREAM_START_STREAM_GRP_PACKET;
	else
		request.message_id = MSG_STREAM_STOP_STREAM_GRP_PACKET;

	request.uid = pipe->group_uid; /*(mixart_uid_t){0,0};*/
	request.data = &group_state;
	request.size = sizeof(group_state);

	err = snd_mixart_send_msg(mgr, &request, sizeof(group_state_resp), &group_state_resp);
	if (err < 0 || group_state_resp.txx_status != 0) {
		snd_printk(KERN_ERR "error MSG_STREAM_ST***_STREAM_GRP_PACKET err=%x stat=%x !\n", err, group_state_resp.txx_status);
		return -EINVAL;
	}

	if(start) {
		u32 stat;

		group_state.pipe_count = 0; /* in case of start same command once again with pipe_count=0 */

		err = snd_mixart_send_msg(mgr, &request, sizeof(group_state_resp), &group_state_resp);
		if (err < 0 || group_state_resp.txx_status != 0) {
			snd_printk(KERN_ERR "error MSG_STREAM_START_STREAM_GRP_PACKET err=%x stat=%x !\n", err, group_state_resp.txx_status);
 			return -EINVAL;
		}

		/* in case of start send a synchro top */

		request.message_id = MSG_SYSTEM_SEND_SYNCHRO_CMD;
		request.uid = (mixart_uid_t){0,0};
		request.data = NULL;
		request.size = 0;

		err = snd_mixart_send_msg(mgr, &request, sizeof(stat), &stat);
		if (err < 0 || stat != 0) {
			snd_printk(KERN_ERR "error MSG_SYSTEM_SEND_SYNCHRO_CMD err=%x stat=%x !\n", err, stat);
			return -EINVAL;
		}

		pipe->status = PIPE_RUNNING;
	}
	else /* !start */
		pipe->status = PIPE_STOPPED;

	return 0;
}


static int mixart_set_clock(mixart_mgr_t *mgr, mixart_pipe_t *pipe, unsigned int rate)
{
	mixart_msg_t request;
	mixart_clock_properties_t clock_properties;
	mixart_clock_properties_resp_t clock_prop_resp;
	int err;

	switch(pipe->status) {
	case PIPE_CLOCK_SET:
		break;
	case PIPE_RUNNING:
		if(rate != 0)
			break;
	default:
		if(rate == 0)
			return 0; /* nothing to do */
		else {
			snd_printk(KERN_ERR "error mixart_set_clock(%d) called with wrong pipe->status !\n", rate);
			return -EINVAL;
		}
	}

	memset(&clock_properties, 0, sizeof(clock_properties));
	clock_properties.clock_generic_type = (rate != 0) ? CGT_INTERNAL_CLOCK : CGT_NO_CLOCK;
	clock_properties.clock_mode = CM_STANDALONE;
	clock_properties.frequency = rate;
	clock_properties.nb_callers = 1; /* only one entry in uid_caller ! */
	clock_properties.uid_caller[0] = pipe->group_uid;

	snd_printdd("mixart_set_clock to %d kHz\n", rate);

	request.message_id = MSG_CLOCK_SET_PROPERTIES;
	request.uid = mgr->uid_console_manager;
	request.data = &clock_properties;
	request.size = sizeof(clock_properties);

	err = snd_mixart_send_msg(mgr, &request, sizeof(clock_prop_resp), &clock_prop_resp);
	if (err < 0 || clock_prop_resp.status != 0 || clock_prop_resp.clock_mode != CM_STANDALONE) {
		snd_printk(KERN_ERR "error MSG_CLOCK_SET_PROPERTIES err=%x stat=%x mod=%x !\n", err, clock_prop_resp.status, clock_prop_resp.clock_mode);
		return -EINVAL;
	}

	if(rate)  pipe->status = PIPE_CLOCK_SET;
	else      pipe->status = PIPE_RUNNING;

	return 0;
}


/*
 *  Allocate or reference output pipe for analog IOs (pcmp0/1)
 */
mixart_pipe_t* snd_mixart_add_ref_pipe( mixart_t *chip, int pcm_number, int capture, int monitoring)
{
	int stream_count;
	mixart_pipe_t *pipe;
	mixart_msg_t request;

	if(capture) {
		if (pcm_number == MIXART_PCM_ANALOG) {
			pipe = &(chip->pipe_in_ana);  /* analog inputs */
		} else {
			pipe = &(chip->pipe_in_dig); /* digital inputs */
		}
		request.message_id = MSG_STREAM_ADD_OUTPUT_GROUP;
		stream_count = MIXART_CAPTURE_STREAMS;
	} else {
		if (pcm_number == MIXART_PCM_ANALOG) {
			pipe = &(chip->pipe_out_ana);  /* analog outputs */
		} else {
			pipe = &(chip->pipe_out_dig);  /* digital outputs */
		}
		request.message_id = MSG_STREAM_ADD_INPUT_GROUP;
		stream_count = MIXART_PLAYBACK_STREAMS;
	}

	/* a new stream is opened and there are already all streams in use */
	if( (monitoring == 0) && (pipe->references >= stream_count) ) {
		return NULL;
	}

	/* pipe is not yet defined */
	if( pipe->status == PIPE_UNDEFINED ) {
		int err, i;
		struct {
			mixart_streaming_group_req_t sgroup_req;
			mixart_streaming_group_t sgroup_resp;
		} *buf;

		snd_printdd("add_ref_pipe audio chip(%d) pcm(%d)\n", chip->chip_idx, pcm_number);

		buf = kmalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf)
			return NULL;

		request.uid = (mixart_uid_t){0,0};      /* should be StreamManagerUID, but zero is OK if there is only one ! */
		request.data = &buf->sgroup_req;
		request.size = sizeof(buf->sgroup_req);

		memset(&buf->sgroup_req, 0, sizeof(buf->sgroup_req));

		buf->sgroup_req.stream_count = stream_count;
		buf->sgroup_req.channel_count = 2;
		buf->sgroup_req.latency = 256;
		buf->sgroup_req.connector = pipe->uid_left_connector;  /* the left connector */

		for (i=0; i<stream_count; i++) {
			int j;
			struct mixart_flowinfo *flowinfo;
			struct mixart_bufferinfo *bufferinfo;
			
			/* we don't yet know the format, so config 16 bit pcm audio for instance */
			buf->sgroup_req.stream_info[i].size_max_byte_frame = 1024;
			buf->sgroup_req.stream_info[i].size_max_sample_frame = 256;
			buf->sgroup_req.stream_info[i].nb_bytes_max_per_sample = MIXART_FLOAT_P__4_0_TO_HEX; /* is 4.0f */

			/* find the right bufferinfo_array */
			j = (chip->chip_idx * MIXART_MAX_STREAM_PER_CARD) + (pcm_number * (MIXART_PLAYBACK_STREAMS + MIXART_CAPTURE_STREAMS)) + i;
			if(capture) j += MIXART_PLAYBACK_STREAMS; /* in the array capture is behind playback */

			buf->sgroup_req.flow_entry[i] = j;

			flowinfo = (struct mixart_flowinfo *)chip->mgr->flowinfo.area;
			flowinfo[j].bufferinfo_array_phy_address = (u32)chip->mgr->bufferinfo.addr + (j * sizeof(mixart_bufferinfo_t));
			flowinfo[j].bufferinfo_count = 1;               /* 1 will set the miXart to ring-buffer mode ! */

			bufferinfo = (struct mixart_bufferinfo *)chip->mgr->bufferinfo.area;
			bufferinfo[j].buffer_address = 0;               /* buffer is not yet allocated */
			bufferinfo[j].available_length = 0;             /* buffer is not yet allocated */

			/* construct the identifier of the stream buffer received in the interrupts ! */
			bufferinfo[j].buffer_id = (chip->chip_idx << MIXART_NOTIFY_CARD_OFFSET) + (pcm_number << MIXART_NOTIFY_PCM_OFFSET ) + i;
			if(capture) {
				bufferinfo[j].buffer_id |= MIXART_NOTIFY_CAPT_MASK;
			}
		}

		err = snd_mixart_send_msg(chip->mgr, &request, sizeof(buf->sgroup_resp), &buf->sgroup_resp);
		if((err < 0) || (buf->sgroup_resp.status != 0)) {
			snd_printk(KERN_ERR "error MSG_STREAM_ADD_**PUT_GROUP err=%x stat=%x !\n", err, buf->sgroup_resp.status);
			kfree(buf);
			return NULL;
		}

		pipe->group_uid = buf->sgroup_resp.group;     /* id of the pipe, as returned by embedded */
		pipe->stream_count = buf->sgroup_resp.stream_count;
		/* pipe->stream_uid[i] = buf->sgroup_resp.stream[i].stream_uid; */

		pipe->status = PIPE_STOPPED;
		kfree(buf);
	}

	if(monitoring)	pipe->monitoring = 1;
	else		pipe->references++;

	return pipe;
}


int snd_mixart_kill_ref_pipe( mixart_mgr_t *mgr, mixart_pipe_t *pipe, int monitoring)
{
	int err = 0;

	if(pipe->status == PIPE_UNDEFINED)
		return 0;

	if(monitoring)
		pipe->monitoring = 0;
	else
		pipe->references--;

	if((pipe->references <= 0) && (pipe->monitoring == 0)) {

		mixart_msg_t request;
		mixart_delete_group_resp_t delete_resp;

		/* release the clock */
		err = mixart_set_clock( mgr, pipe, 0);
		if( err < 0 ) {
			snd_printk(KERN_ERR "mixart_set_clock(0) return error!\n");
		}

		/* stop the pipe */
		err = mixart_set_pipe_state(mgr, pipe, 0);
		if( err < 0 ) {
			snd_printk(KERN_ERR "error stopping pipe!\n");
		}

		request.message_id = MSG_STREAM_DELETE_GROUP;
		request.uid = (mixart_uid_t){0,0};
		request.data = &pipe->group_uid;            /* the streaming group ! */
		request.size = sizeof(pipe->group_uid);

		/* delete the pipe */
		err = snd_mixart_send_msg(mgr, &request, sizeof(delete_resp), &delete_resp);
		if ((err < 0) || (delete_resp.status != 0)) {
			snd_printk(KERN_ERR "error MSG_STREAM_DELETE_GROUP err(%x), status(%x)\n", err, delete_resp.status);
		}

		pipe->group_uid = (mixart_uid_t){0,0};
		pipe->stream_count = 0;
		pipe->status = PIPE_UNDEFINED;
	}

	return err;
}

static int mixart_set_stream_state(mixart_stream_t *stream, int start)
{
	mixart_t *chip;
	mixart_stream_state_req_t stream_state_req;
	mixart_msg_t request;

	if(!stream->substream)
		return -EINVAL;

	memset(&stream_state_req, 0, sizeof(stream_state_req));
	stream_state_req.stream_count = 1;
	stream_state_req.stream_info.stream_desc.uid_pipe = stream->pipe->group_uid;
	stream_state_req.stream_info.stream_desc.stream_idx = stream->substream->number;

	if (stream->substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		request.message_id = start ? MSG_STREAM_START_INPUT_STAGE_PACKET : MSG_STREAM_STOP_INPUT_STAGE_PACKET;
	else
		request.message_id = start ? MSG_STREAM_START_OUTPUT_STAGE_PACKET : MSG_STREAM_STOP_OUTPUT_STAGE_PACKET;

	request.uid = (mixart_uid_t){0,0};
	request.data = &stream_state_req;
	request.size = sizeof(stream_state_req);

	stream->abs_period_elapsed = 0;            /* reset stream pos      */
	stream->buf_periods = 0;
	stream->buf_period_frag = 0;

	chip = snd_pcm_substream_chip(stream->substream);

	return snd_mixart_send_msg_nonblock(chip->mgr, &request);
}

/*
 *  Trigger callback
 */

static int snd_mixart_trigger(snd_pcm_substream_t *subs, int cmd)
{
	mixart_stream_t *stream = (mixart_stream_t*)subs->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:

		snd_printdd("SNDRV_PCM_TRIGGER_START\n");

		/* START_STREAM */
		if( mixart_set_stream_state(stream, 1) )
			return -EINVAL;

		stream->status = MIXART_STREAM_STATUS_RUNNING;

		break;
	case SNDRV_PCM_TRIGGER_STOP:

		/* STOP_STREAM */
		if( mixart_set_stream_state(stream, 0) )
			return -EINVAL;

		stream->status = MIXART_STREAM_STATUS_OPEN;

		snd_printdd("SNDRV_PCM_TRIGGER_STOP\n");

		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* TODO */
		stream->status = MIXART_STREAM_STATUS_PAUSE;
		snd_printdd("SNDRV_PCM_PAUSE_PUSH\n");
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* TODO */
		stream->status = MIXART_STREAM_STATUS_RUNNING;
		snd_printdd("SNDRV_PCM_PAUSE_RELEASE\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int mixart_sync_nonblock_events(mixart_mgr_t *mgr)
{
	unsigned long timeout = jiffies + HZ;
	while (atomic_read(&mgr->msg_processed) > 0) {
		if (time_after(jiffies, timeout)) {
			snd_printk(KERN_ERR "mixart: cannot process nonblock events!\n");
			return -EBUSY;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	}
	return 0;
}

/*
 *  prepare callback for all pcms
 */
static int snd_mixart_prepare(snd_pcm_substream_t *subs)
{
	mixart_t *chip = snd_pcm_substream_chip(subs);
	mixart_stream_t *stream = (mixart_stream_t*)subs->runtime->private_data;

	/* TODO de façon non bloquante, réappliquer les hw_params (rate, bits, codec) */

	snd_printdd("snd_mixart_prepare\n");

	mixart_sync_nonblock_events(chip->mgr);

	/* only the first stream can choose the sample rate */
	/* the further opened streams will be limited to its frequency (see open) */
	if(chip->mgr->ref_count_rate == 1)
		chip->mgr->sample_rate = subs->runtime->rate;

	/* set the clock only once (first stream) on the same pipe */
	if(stream->pipe->references == 1) {
		if( mixart_set_clock(chip->mgr, stream->pipe, subs->runtime->rate) )
			return -EINVAL;
	}

	return 0;
}


static int mixart_set_format(mixart_stream_t *stream, snd_pcm_format_t format)
{
	int err;
	mixart_t *chip;
	mixart_msg_t request;
	mixart_stream_param_desc_t stream_param;
	mixart_return_uid_t resp;

	chip = snd_pcm_substream_chip(stream->substream);

	memset(&stream_param, 0, sizeof(stream_param));

	stream_param.coding_type = CT_LINEAR;
	stream_param.number_of_channel = stream->channels;

	stream_param.sampling_freq = chip->mgr->sample_rate;
	if(stream_param.sampling_freq == 0)
		stream_param.sampling_freq = 44100; /* if frequency not yet defined, use some default */

	switch(format){
	case SNDRV_PCM_FORMAT_U8:
		stream_param.sample_type = ST_INTEGER_8;
		stream_param.sample_size = 8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		stream_param.sample_type = ST_INTEGER_16LE;
		stream_param.sample_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		stream_param.sample_type = ST_INTEGER_16BE;
		stream_param.sample_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_3LE:
		stream_param.sample_type = ST_INTEGER_24LE;
		stream_param.sample_size = 24;
		break;
	case SNDRV_PCM_FORMAT_S24_3BE:
		stream_param.sample_type = ST_INTEGER_24BE;
		stream_param.sample_size = 24;
		break;
	case SNDRV_PCM_FORMAT_FLOAT_LE:
		stream_param.sample_type = ST_FLOATING_POINT_32LE;
		stream_param.sample_size = 32;
		break;
	case  SNDRV_PCM_FORMAT_FLOAT_BE:
		stream_param.sample_type = ST_FLOATING_POINT_32BE;
		stream_param.sample_size = 32;
		break;
	default:
		snd_printk(KERN_ERR "error mixart_set_format() : unknown format\n");
		return -EINVAL;
	}

	snd_printdd("set SNDRV_PCM_FORMAT sample_type(%d) sample_size(%d) freq(%d) channels(%d)\n",
		   stream_param.sample_type, stream_param.sample_size, stream_param.sampling_freq, stream->channels);

	/* TODO: what else to configure ? */
	/* stream_param.samples_per_frame = 2; */
	/* stream_param.bytes_per_frame = 4; */
	/* stream_param.bytes_per_sample = 2; */

	stream_param.pipe_count = 1;      /* set to 1 */
	stream_param.stream_count = 1;    /* set to 1 */
	stream_param.stream_desc[0].uid_pipe = stream->pipe->group_uid;
	stream_param.stream_desc[0].stream_idx = stream->substream->number;

	request.message_id = MSG_STREAM_SET_INPUT_STAGE_PARAM;
	request.uid = (mixart_uid_t){0,0};
	request.data = &stream_param;
	request.size = sizeof(stream_param);

	err = snd_mixart_send_msg(chip->mgr, &request, sizeof(resp), &resp);
	if((err < 0) || resp.error_code) {
		snd_printk(KERN_ERR "MSG_STREAM_SET_INPUT_STAGE_PARAM err=%x; resp=%x\n", err, resp.error_code);
		return -EINVAL;
	}
	return 0;
}


/*
 *  HW_PARAMS callback for all pcms
 */
static int snd_mixart_hw_params(snd_pcm_substream_t *subs,
                                snd_pcm_hw_params_t *hw)
{
	mixart_t *chip = snd_pcm_substream_chip(subs);
	mixart_mgr_t *mgr = chip->mgr;
	mixart_stream_t *stream = (mixart_stream_t*)subs->runtime->private_data;
	snd_pcm_format_t format;
	int err;
	int channels;

	/* set up channels */
	channels = params_channels(hw);

	/*  set up format for the stream */
	format = params_format(hw);

	down(&mgr->setup_mutex);

	/* update the stream levels */
	if( stream->pcm_number <= MIXART_PCM_DIGITAL ) {
		int is_aes = stream->pcm_number > MIXART_PCM_ANALOG;
		if( subs->stream == SNDRV_PCM_STREAM_PLAYBACK )
			mixart_update_playback_stream_level(chip, is_aes, subs->number);
		else
			mixart_update_capture_stream_level( chip, is_aes);
	}

	stream->channels = channels;

	/* set the format to the board */
	err = mixart_set_format(stream, format);
	if(err < 0) {
		return err;
	}

	/* allocate buffer */
	err = snd_pcm_lib_malloc_pages(subs, params_buffer_bytes(hw));

	if (err > 0) {
		struct mixart_bufferinfo *bufferinfo;
		int i = (chip->chip_idx * MIXART_MAX_STREAM_PER_CARD) + (stream->pcm_number * (MIXART_PLAYBACK_STREAMS+MIXART_CAPTURE_STREAMS)) + subs->number;
		if( subs->stream == SNDRV_PCM_STREAM_CAPTURE ) {
			i += MIXART_PLAYBACK_STREAMS; /* in array capture is behind playback */
		}
		
		bufferinfo = (struct mixart_bufferinfo *)chip->mgr->bufferinfo.area;
		bufferinfo[i].buffer_address = subs->runtime->dma_addr;
		bufferinfo[i].available_length = subs->runtime->dma_bytes;
		/* bufferinfo[i].buffer_id  is already defined */

		snd_printdd("snd_mixart_hw_params(pcm %d) : dma_addr(%x) dma_bytes(%x) subs-number(%d)\n", i,
				bufferinfo[i].buffer_address,
				bufferinfo[i].available_length,
				subs->number);
	}
	up(&mgr->setup_mutex);

	return err;
}

static int snd_mixart_hw_free(snd_pcm_substream_t *subs)
{
	mixart_t *chip = snd_pcm_substream_chip(subs);
	snd_pcm_lib_free_pages(subs);
	mixart_sync_nonblock_events(chip->mgr);
	return 0;
}



/*
 *  TODO CONFIGURATION SPACE for all pcms, mono pcm must update channels_max
 */
static snd_pcm_hardware_t snd_mixart_analog_caps =
{
	.info             = ( SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START |
			      SNDRV_PCM_INFO_PAUSE),
	.formats	  = ( SNDRV_PCM_FMTBIT_U8 |
			      SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
			      SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |
			      SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE ),
	.rates            = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min         = 8000,
	.rate_max         = 48000,
	.channels_min     = 1,
	.channels_max     = 2,
	.buffer_bytes_max = (32*1024),
	.period_bytes_min = 256,                  /* 256 frames U8 mono*/
	.period_bytes_max = (16*1024),
	.periods_min      = 2,
	.periods_max      = (32*1024/256),
};

static snd_pcm_hardware_t snd_mixart_digital_caps =
{
	.info             = ( SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START |
			      SNDRV_PCM_INFO_PAUSE),
	.formats	  = ( SNDRV_PCM_FMTBIT_U8 |
			      SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
			      SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |
			      SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE ),
	.rates            = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min         = 32000,
	.rate_max         = 48000,
	.channels_min     = 1,
	.channels_max     = 2,
	.buffer_bytes_max = (32*1024),
	.period_bytes_min = 256,                  /* 256 frames U8 mono*/
	.period_bytes_max = (16*1024),
	.periods_min      = 2,
	.periods_max      = (32*1024/256),
};


static int snd_mixart_playback_open(snd_pcm_substream_t *subs)
{
	mixart_t            *chip = snd_pcm_substream_chip(subs);
	mixart_mgr_t        *mgr = chip->mgr;
	snd_pcm_runtime_t   *runtime = subs->runtime;
	snd_pcm_t           *pcm = subs->pcm;
	mixart_stream_t     *stream;
	mixart_pipe_t       *pipe;
	int err = 0;
	int pcm_number;

	down(&mgr->setup_mutex);

	if ( pcm == chip->pcm ) {
		pcm_number = MIXART_PCM_ANALOG;
		runtime->hw = snd_mixart_analog_caps;
	} else {
		snd_assert ( pcm == chip->pcm_dig ); 
		pcm_number = MIXART_PCM_DIGITAL;
		runtime->hw = snd_mixart_digital_caps;
	}
	snd_printdd("snd_mixart_playback_open C%d/P%d/Sub%d\n", chip->chip_idx, pcm_number, subs->number);

	/* get stream info */
	stream = &(chip->playback_stream[pcm_number][subs->number]);

	if (stream->status != MIXART_STREAM_STATUS_FREE){
		/* streams in use */
		snd_printk(KERN_ERR "snd_mixart_playback_open C%d/P%d/Sub%d in use\n", chip->chip_idx, pcm_number, subs->number);
		err = -EBUSY;
		goto _exit_open;
	}

	/* get pipe pointer (out pipe) */
	pipe = snd_mixart_add_ref_pipe(chip, pcm_number, 0, 0);

	if (pipe == NULL) {
		err = -EINVAL;
		goto _exit_open;
	}

	/* start the pipe if necessary */
	err = mixart_set_pipe_state(chip->mgr, pipe, 1);
	if( err < 0 ) {
		snd_printk(KERN_ERR "error starting pipe!\n");
		snd_mixart_kill_ref_pipe(chip->mgr, pipe, 0);
		err = -EINVAL;
		goto _exit_open;
	}

	stream->pipe        = pipe;
	stream->pcm_number  = pcm_number;
	stream->status      = MIXART_STREAM_STATUS_OPEN;
	stream->substream   = subs;
	stream->channels    = 0; /* not configured yet */

	runtime->private_data = stream;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 64);

	/* if a sample rate is already used, another stream cannot change */
	if(mgr->ref_count_rate++) {
		if(mgr->sample_rate) {
			runtime->hw.rate_min = runtime->hw.rate_max = mgr->sample_rate;
		}
	}

 _exit_open:
	up(&mgr->setup_mutex);

	return err;
}


static int snd_mixart_capture_open(snd_pcm_substream_t *subs)
{
	mixart_t            *chip = snd_pcm_substream_chip(subs);
	mixart_mgr_t        *mgr = chip->mgr;
	snd_pcm_runtime_t   *runtime = subs->runtime;
	snd_pcm_t           *pcm = subs->pcm;
	mixart_stream_t     *stream;
	mixart_pipe_t       *pipe;
	int err = 0;
	int pcm_number;

	down(&mgr->setup_mutex);

	if ( pcm == chip->pcm ) {
		pcm_number = MIXART_PCM_ANALOG;
		runtime->hw = snd_mixart_analog_caps;
	} else {
		snd_assert ( pcm == chip->pcm_dig ); 
		pcm_number = MIXART_PCM_DIGITAL;
		runtime->hw = snd_mixart_digital_caps;
	}

	runtime->hw.channels_min = 2; /* for instance, no mono */

	snd_printdd("snd_mixart_capture_open C%d/P%d/Sub%d\n", chip->chip_idx, pcm_number, subs->number);

	/* get stream info */
	stream = &(chip->capture_stream[pcm_number]);

	if (stream->status != MIXART_STREAM_STATUS_FREE){
		/* streams in use */
		snd_printk(KERN_ERR "snd_mixart_capture_open C%d/P%d/Sub%d in use\n", chip->chip_idx, pcm_number, subs->number);
		err = -EBUSY;
		goto _exit_open;
	}

	/* get pipe pointer (in pipe) */
	pipe = snd_mixart_add_ref_pipe(chip, pcm_number, 1, 0);

	if (pipe == NULL) {
		err = -EINVAL;
		goto _exit_open;
	}

	/* start the pipe if necessary */
	err = mixart_set_pipe_state(chip->mgr, pipe, 1);
	if( err < 0 ) {
		snd_printk(KERN_ERR "error starting pipe!\n");
		snd_mixart_kill_ref_pipe(chip->mgr, pipe, 0);
		err = -EINVAL;
		goto _exit_open;
	}

	stream->pipe        = pipe;
	stream->pcm_number  = pcm_number;
	stream->status      = MIXART_STREAM_STATUS_OPEN;
	stream->substream   = subs;
	stream->channels    = 0; /* not configured yet */

	runtime->private_data = stream;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 64);

	/* if a sample rate is already used, another stream cannot change */
	if(mgr->ref_count_rate++) {
		if(mgr->sample_rate) {
			runtime->hw.rate_min = runtime->hw.rate_max = mgr->sample_rate;
		}
	}

 _exit_open:
	up(&mgr->setup_mutex);

	return err;
}



static int snd_mixart_close(snd_pcm_substream_t *subs)
{
	mixart_t *chip = snd_pcm_substream_chip(subs);
	mixart_mgr_t *mgr = chip->mgr;
	mixart_stream_t *stream = (mixart_stream_t*)subs->runtime->private_data;

	down(&mgr->setup_mutex);

	snd_printdd("snd_mixart_close C%d/P%d/Sub%d\n", chip->chip_idx, stream->pcm_number, subs->number);

	/* sample rate released */
	if(--mgr->ref_count_rate == 0) {
		mgr->sample_rate = 0;
	}

	/* delete pipe */
	if (snd_mixart_kill_ref_pipe(mgr, stream->pipe, 0 ) < 0) {

		snd_printk(KERN_ERR "error snd_mixart_kill_ref_pipe C%dP%d\n", chip->chip_idx, stream->pcm_number);
	}

	stream->pipe      = NULL;
	stream->status    = MIXART_STREAM_STATUS_FREE;
	stream->substream = NULL;

	up(&mgr->setup_mutex);
	return 0;
}


static snd_pcm_uframes_t snd_mixart_stream_pointer(snd_pcm_substream_t * subs)
{
	snd_pcm_runtime_t *runtime = subs->runtime;
	mixart_stream_t   *stream  = (mixart_stream_t*)runtime->private_data;

	return (snd_pcm_uframes_t)((stream->buf_periods * runtime->period_size) + stream->buf_period_frag);
}



static snd_pcm_ops_t snd_mixart_playback_ops = {
	.open      = snd_mixart_playback_open,
	.close     = snd_mixart_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.prepare   = snd_mixart_prepare,
	.hw_params = snd_mixart_hw_params,
	.hw_free   = snd_mixart_hw_free,
	.trigger   = snd_mixart_trigger,
	.pointer   = snd_mixart_stream_pointer,
};

static snd_pcm_ops_t snd_mixart_capture_ops = {
	.open      = snd_mixart_capture_open,
	.close     = snd_mixart_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.prepare   = snd_mixart_prepare,
	.hw_params = snd_mixart_hw_params,
	.hw_free   = snd_mixart_hw_free,
	.trigger   = snd_mixart_trigger,
	.pointer   = snd_mixart_stream_pointer,
};

static void preallocate_buffers(mixart_t *chip, snd_pcm_t *pcm)
{
#if 0
	snd_pcm_substream_t *subs;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		int idx = 0;
		for (subs = pcm->streams[stream].substream; subs; subs = subs->next, idx++)
			/* set up the unique device id with the chip index */
			subs->dma_device.id = subs->pcm->device << 16 |
				subs->stream << 8 | (subs->number + 1) |
				(chip->chip_idx + 1) << 24;
	}
#endif
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->mgr->pci), 32*1024, 32*1024);
}

/*
 */
static int snd_mixart_pcm_analog(mixart_t *chip)
{
	int err;
	snd_pcm_t *pcm;
	char name[32];

	sprintf(name, "miXart analog %d", chip->chip_idx);
	if ((err = snd_pcm_new(chip->card, name, MIXART_PCM_ANALOG,
			       MIXART_PLAYBACK_STREAMS,
			       MIXART_CAPTURE_STREAMS, &pcm)) < 0) {
		snd_printk(KERN_ERR "cannot create the analog pcm %d\n", chip->chip_idx);
		return err;
	}

	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_mixart_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_mixart_capture_ops);

	pcm->info_flags = 0;
	strcpy(pcm->name, name);

	preallocate_buffers(chip, pcm);

	chip->pcm = pcm;
	return 0;
}


/*
 */
static int snd_mixart_pcm_digital(mixart_t *chip)
{
	int err;
	snd_pcm_t *pcm;
	char name[32];

	sprintf(name, "miXart AES/EBU %d", chip->chip_idx);
	if ((err = snd_pcm_new(chip->card, name, MIXART_PCM_DIGITAL,
			       MIXART_PLAYBACK_STREAMS,
			       MIXART_CAPTURE_STREAMS, &pcm)) < 0) {
		snd_printk(KERN_ERR "cannot create the digital pcm %d\n", chip->chip_idx);
		return err;
	}

	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_mixart_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_mixart_capture_ops);

	pcm->info_flags = 0;
	strcpy(pcm->name, name);

	preallocate_buffers(chip, pcm);

	chip->pcm_dig = pcm;
	return 0;
}

static int snd_mixart_chip_free(mixart_t *chip)
{
	kfree(chip);
	return 0;
}

static int snd_mixart_chip_dev_free(snd_device_t *device)
{
	mixart_t *chip = device->device_data;
	return snd_mixart_chip_free(chip);
}


/*
 */
static int __devinit snd_mixart_create(mixart_mgr_t *mgr, snd_card_t *card, int idx)
{
	int err;
	mixart_t *chip;
	static snd_device_ops_t ops = {
		.dev_free = snd_mixart_chip_dev_free,
	};

	mgr->chip[idx] = chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (! chip) {
		snd_printk(KERN_ERR "cannot allocate chip\n");
		return -ENOMEM;
	}

	chip->card = card;
	chip->chip_idx = idx;
	chip->mgr = mgr;

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_mixart_chip_free(chip);
		return err;
	}

	snd_card_set_dev(card, &mgr->pci->dev);

	return 0;
}

int snd_mixart_create_pcm(mixart_t* chip)
{
	int err;

	err = snd_mixart_pcm_analog(chip);
	if (err < 0)
		return err;

	if(chip->mgr->board_type == MIXART_DAUGHTER_TYPE_AES) {

		err = snd_mixart_pcm_digital(chip);
		if (err < 0)
			return err;
	}
	return err;
}


/*
 * release all the cards assigned to a manager instance
 */
static int snd_mixart_free(mixart_mgr_t *mgr)
{
	unsigned int i;

	for (i = 0; i < mgr->num_cards; i++) {
		if (mgr->chip[i])
			snd_card_free(mgr->chip[i]->card);
	}

	/* stop mailbox */
	snd_mixart_exit_mailbox(mgr);

	/* release irq  */
	if (mgr->irq >= 0)
		free_irq(mgr->irq, (void *)mgr);

	/* reset board if some firmware was loaded */
	if(mgr->dsp_loaded) {
		snd_mixart_reset_board(mgr);
		snd_printdd("reset miXart !\n");
	}

	/* release the i/o ports */
	for (i = 0; i < 2; i++) {
		if (mgr->mem[i].virt)
			iounmap(mgr->mem[i].virt);
	}
	pci_release_regions(mgr->pci);

	/* free flowarray */
	if(mgr->flowinfo.area) {
		snd_dma_free_pages(&mgr->flowinfo);
		mgr->flowinfo.area = NULL;
	}
	/* free bufferarray */
	if(mgr->bufferinfo.area) {
		snd_dma_free_pages(&mgr->bufferinfo);
		mgr->bufferinfo.area = NULL;
	}

	pci_disable_device(mgr->pci);
	kfree(mgr);
	return 0;
}

/*
 * proc interface
 */
static long long snd_mixart_BA0_llseek(snd_info_entry_t *entry,
				       void *private_file_data,
				       struct file *file,
				       long long offset,
				       int orig)
{
	offset = offset & ~3; /* 4 bytes aligned */

	switch(orig) {
	case 0:  /* SEEK_SET */
		file->f_pos = offset;
		break;
	case 1:  /* SEEK_CUR */
		file->f_pos += offset;
		break;
	case 2:  /* SEEK_END, offset is negative */
		file->f_pos = MIXART_BA0_SIZE + offset;
		break;
	default:
		return -EINVAL;
	}
	if(file->f_pos > MIXART_BA0_SIZE)
		file->f_pos = MIXART_BA0_SIZE;
	return file->f_pos;
}

static long long snd_mixart_BA1_llseek(snd_info_entry_t *entry,
				       void *private_file_data,
				       struct file *file,
				       long long offset,
				       int orig)
{
	offset = offset & ~3; /* 4 bytes aligned */

	switch(orig) {
	case 0:  /* SEEK_SET */
		file->f_pos = offset;
		break;
	case 1:  /* SEEK_CUR */
		file->f_pos += offset;
		break;
	case 2: /* SEEK_END, offset is negative */
		file->f_pos = MIXART_BA1_SIZE + offset;
		break;
	default:
		return -EINVAL;
	}
	if(file->f_pos > MIXART_BA1_SIZE)
		file->f_pos = MIXART_BA1_SIZE;
	return file->f_pos;
}

/*
  mixart_BA0 proc interface for BAR 0 - read callback
 */
static long snd_mixart_BA0_read(snd_info_entry_t *entry, void *file_private_data,
				struct file *file, char __user *buf,
				unsigned long count, unsigned long pos)
{
	mixart_mgr_t *mgr = entry->private_data;

	count = count & ~3; /* make sure the read size is a multiple of 4 bytes */
	if(count <= 0)
		return 0;
	if(pos + count > MIXART_BA0_SIZE)
		count = (long)(MIXART_BA0_SIZE - pos);
	if(copy_to_user_fromio(buf, MIXART_MEM( mgr, pos ), count))
		return -EFAULT;
	return count;
}

/*
  mixart_BA1 proc interface for BAR 1 - read callback
 */
static long snd_mixart_BA1_read(snd_info_entry_t *entry, void *file_private_data,
				struct file *file, char __user *buf,
				unsigned long count, unsigned long pos)
{
	mixart_mgr_t *mgr = entry->private_data;

	count = count & ~3; /* make sure the read size is a multiple of 4 bytes */
	if(count <= 0)
		return 0;
	if(pos + count > MIXART_BA1_SIZE)
		count = (long)(MIXART_BA1_SIZE - pos);
	if(copy_to_user_fromio(buf, MIXART_REG( mgr, pos ), count))
		return -EFAULT;
	return count;
}

static struct snd_info_entry_ops snd_mixart_proc_ops_BA0 = {
	.read   = snd_mixart_BA0_read,
	.llseek = snd_mixart_BA0_llseek
};

static struct snd_info_entry_ops snd_mixart_proc_ops_BA1 = {
	.read   = snd_mixart_BA1_read,
	.llseek = snd_mixart_BA1_llseek
};


static void snd_mixart_proc_read(snd_info_entry_t *entry, 
                                 snd_info_buffer_t * buffer)
{
	mixart_t *chip = entry->private_data;        
	u32 ref; 

	snd_iprintf(buffer, "Digigram miXart (alsa card %d)\n\n", chip->chip_idx);

	/* stats available when embedded OS is running */
	if (chip->mgr->dsp_loaded & ( 1 << MIXART_MOTHERBOARD_ELF_INDEX)) {
		snd_iprintf(buffer, "- hardware -\n");
		switch (chip->mgr->board_type ) {
		case MIXART_DAUGHTER_TYPE_NONE     : snd_iprintf(buffer, "\tmiXart8 (no daughter board)\n\n"); break;
		case MIXART_DAUGHTER_TYPE_AES      : snd_iprintf(buffer, "\tmiXart8 AES/EBU\n\n"); break;
		case MIXART_DAUGHTER_TYPE_COBRANET : snd_iprintf(buffer, "\tmiXart8 Cobranet\n\n"); break;
		default:                             snd_iprintf(buffer, "\tUNKNOWN!\n\n"); break;
		}

		snd_iprintf(buffer, "- system load -\n");	 

		/* get perf reference */

		ref = readl_be( MIXART_MEM( chip->mgr, MIXART_PSEUDOREG_PERF_SYSTEM_LOAD_OFFSET));

		if (ref) {
			u32 mailbox   = 100 * readl_be( MIXART_MEM( chip->mgr, MIXART_PSEUDOREG_PERF_MAILBX_LOAD_OFFSET)) / ref;
			u32 streaming = 100 * readl_be( MIXART_MEM( chip->mgr, MIXART_PSEUDOREG_PERF_STREAM_LOAD_OFFSET)) / ref;
			u32 interr    = 100 * readl_be( MIXART_MEM( chip->mgr, MIXART_PSEUDOREG_PERF_INTERR_LOAD_OFFSET)) / ref;

			snd_iprintf(buffer, "\tstreaming          : %d\n", streaming);
			snd_iprintf(buffer, "\tmailbox            : %d\n", mailbox);
			snd_iprintf(buffer, "\tinterrups handling : %d\n\n", interr);
		}
	} /* endif elf loaded */
}

static void __devinit snd_mixart_proc_init(mixart_t *chip)
{
	snd_info_entry_t *entry;

	/* text interface to read perf and temp meters */
	if (! snd_card_proc_new(chip->card, "board_info", &entry)) {
		entry->private_data = chip;
		entry->c.text.read_size = 1024;
		entry->c.text.read = snd_mixart_proc_read;
	}

	if (! snd_card_proc_new(chip->card, "mixart_BA0", &entry)) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = chip->mgr;	
		entry->c.ops = &snd_mixart_proc_ops_BA0;
		entry->size = MIXART_BA0_SIZE;
	}
	if (! snd_card_proc_new(chip->card, "mixart_BA1", &entry)) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = chip->mgr;
		entry->c.ops = &snd_mixart_proc_ops_BA1;
		entry->size = MIXART_BA1_SIZE;
	}
}
/* end of proc interface */


/*
 *    probe function - creates the card manager
 */
static int __devinit snd_mixart_probe(struct pci_dev *pci,
				      const struct pci_device_id *pci_id)
{
	static int dev;
	mixart_mgr_t *mgr;
	unsigned int i;
	int err;
	size_t size;

	/*
	 */
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (! enable[dev]) {
		dev++;
		return -ENOENT;
	}

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	pci_set_master(pci);

	/* check if we can restrict PCI DMA transfers to 32 bits */
	if (pci_set_dma_mask(pci, 0xffffffff) < 0) {
		snd_printk(KERN_ERR "architecture does not support 32bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	/*
	 */
	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (! mgr) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	mgr->pci = pci;
	mgr->irq = -1;

	/* resource assignment */
	if ((err = pci_request_regions(pci, CARD_NAME)) < 0) {
		kfree(mgr);
		pci_disable_device(pci);
		return err;
	}
	for (i = 0; i < 2; i++) {
		mgr->mem[i].phys = pci_resource_start(pci, i);
		mgr->mem[i].virt = ioremap_nocache(mgr->mem[i].phys,
						   pci_resource_len(pci, i));
	}

	if (request_irq(pci->irq, snd_mixart_interrupt, SA_INTERRUPT|SA_SHIRQ, CARD_NAME, (void *)mgr)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_mixart_free(mgr);
		return -EBUSY;
	}
	mgr->irq = pci->irq;

	sprintf(mgr->shortname, "Digigram miXart");
	sprintf(mgr->longname, "%s at 0x%lx & 0x%lx, irq %i", mgr->shortname, mgr->mem[0].phys, mgr->mem[1].phys, mgr->irq);

	/* ISR spinlock  */
	spin_lock_init(&mgr->lock);

	/* init mailbox  */
	mgr->msg_fifo_readptr = 0;
	mgr->msg_fifo_writeptr = 0;

	spin_lock_init(&mgr->msg_lock);
	init_MUTEX(&mgr->msg_mutex);
	init_waitqueue_head(&mgr->msg_sleep);
	atomic_set(&mgr->msg_processed, 0);

	/* init setup mutex*/
	init_MUTEX(&mgr->setup_mutex);

	/* init message taslket */
	tasklet_init( &mgr->msg_taskq, snd_mixart_msg_tasklet, (unsigned long) mgr);

	/* card assignment */
	mgr->num_cards = MIXART_MAX_CARDS; /* 4  FIXME: configurable? */
	for (i = 0; i < mgr->num_cards; i++) {
		snd_card_t *card;
		char tmpid[16];
		int idx;

		if (index[dev] < 0)
			idx = index[dev];
		else
			idx = index[dev] + i;
		snprintf(tmpid, sizeof(tmpid), "%s-%d", id[dev] ? id[dev] : "MIXART", i);
		card = snd_card_new(idx, tmpid, THIS_MODULE, 0);

		if (! card) {
			snd_printk(KERN_ERR "cannot allocate the card %d\n", i);
			snd_mixart_free(mgr);
			return -ENOMEM;
		}

		strcpy(card->driver, CARD_NAME);
		sprintf(card->shortname, "%s [PCM #%d]", mgr->shortname, i);
		sprintf(card->longname, "%s [PCM #%d]", mgr->longname, i);

		if ((err = snd_mixart_create(mgr, card, i)) < 0) {
			snd_mixart_free(mgr);
			return err;
		}

		if(i==0) {
			/* init proc interface only for chip0 */
			snd_mixart_proc_init(mgr->chip[i]);
		}

		if ((err = snd_card_register(card)) < 0) {
			snd_mixart_free(mgr);
			return err;
		}
	}

	/* init firmware status (mgr->dsp_loaded reset in hwdep_new) */
	mgr->board_type = MIXART_DAUGHTER_TYPE_NONE;

	/* create array of streaminfo */
	size = PAGE_ALIGN( (MIXART_MAX_STREAM_PER_CARD * MIXART_MAX_CARDS * sizeof(mixart_flowinfo_t)) );
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
				size, &mgr->flowinfo) < 0) {
		snd_mixart_free(mgr);
		return -ENOMEM;
	}
	/* init streaminfo_array */
	memset(mgr->flowinfo.area, 0, size);

	/* create array of bufferinfo */
	size = PAGE_ALIGN( (MIXART_MAX_STREAM_PER_CARD * MIXART_MAX_CARDS * sizeof(mixart_bufferinfo_t)) );
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
				size, &mgr->bufferinfo) < 0) {
		snd_mixart_free(mgr);
		return -ENOMEM;
	}
	/* init bufferinfo_array */
	memset(mgr->bufferinfo.area, 0, size);

	/* set up firmware */
	err = snd_mixart_setup_firmware(mgr);
	if (err < 0) {
		snd_mixart_free(mgr);
		return err;
	}

	pci_set_drvdata(pci, mgr);
	dev++;
	return 0;
}

static void __devexit snd_mixart_remove(struct pci_dev *pci)
{
	snd_mixart_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "Digigram miXart",
	.owner = THIS_MODULE,
	.id_table = snd_mixart_ids,
	.probe = snd_mixart_probe,
	.remove = __devexit_p(snd_mixart_remove),
};

static int __init alsa_card_mixart_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_mixart_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_mixart_init)
module_exit(alsa_card_mixart_exit)
