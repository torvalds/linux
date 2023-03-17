// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017-2021 NXP

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/rpmsg.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>

#include "imx-pcm.h"
#include "fsl_rpmsg.h"
#include "imx-pcm-rpmsg.h"

static struct snd_pcm_hardware imx_rpmsg_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.buffer_bytes_max = IMX_DEFAULT_DMABUF_SIZE,
	.period_bytes_min = 512,
	.period_bytes_max = 65536,
	.periods_min = 2,
	.periods_max = 6000,
	.fifo_size = 0,
};

static int imx_rpmsg_pcm_send_message(struct rpmsg_msg *msg,
				      struct rpmsg_info *info)
{
	struct rpmsg_device *rpdev = info->rpdev;
	int ret = 0;

	mutex_lock(&info->msg_lock);
	if (!rpdev) {
		dev_err(info->dev, "rpmsg channel not ready\n");
		mutex_unlock(&info->msg_lock);
		return -EINVAL;
	}

	dev_dbg(&rpdev->dev, "send cmd %d\n", msg->s_msg.header.cmd);

	if (!(msg->s_msg.header.type == MSG_TYPE_C))
		reinit_completion(&info->cmd_complete);

	ret = rpmsg_send(rpdev->ept, (void *)&msg->s_msg,
			 sizeof(struct rpmsg_s_msg));
	if (ret) {
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
		mutex_unlock(&info->msg_lock);
		return ret;
	}

	/* No receive msg for TYPE_C command */
	if (msg->s_msg.header.type == MSG_TYPE_C) {
		mutex_unlock(&info->msg_lock);
		return 0;
	}

	/* wait response from rpmsg */
	ret = wait_for_completion_timeout(&info->cmd_complete,
					  msecs_to_jiffies(RPMSG_TIMEOUT));
	if (!ret) {
		dev_err(&rpdev->dev, "rpmsg_send cmd %d timeout!\n",
			msg->s_msg.header.cmd);
		mutex_unlock(&info->msg_lock);
		return -ETIMEDOUT;
	}

	memcpy(&msg->r_msg, &info->r_msg, sizeof(struct rpmsg_r_msg));
	memcpy(&info->msg[msg->r_msg.header.cmd].r_msg,
	       &msg->r_msg, sizeof(struct rpmsg_r_msg));

	/*
	 * Reset the buffer pointer to be zero, actully we have
	 * set the buffer pointer to be zero in imx_rpmsg_terminate_all
	 * But if there is timer task queued in queue, after it is
	 * executed the buffer pointer will be changed, so need to
	 * reset it again with TERMINATE command.
	 */
	switch (msg->s_msg.header.cmd) {
	case TX_TERMINATE:
		info->msg[TX_POINTER].r_msg.param.buffer_offset = 0;
		break;
	case RX_TERMINATE:
		info->msg[RX_POINTER].r_msg.param.buffer_offset = 0;
		break;
	default:
		break;
	}

	dev_dbg(&rpdev->dev, "cmd:%d, resp %d\n", msg->s_msg.header.cmd,
		info->r_msg.param.resp);

	mutex_unlock(&info->msg_lock);

	return 0;
}

static int imx_rpmsg_insert_workqueue(struct snd_pcm_substream *substream,
				      struct rpmsg_msg *msg,
				      struct rpmsg_info *info)
{
	unsigned long flags;
	int ret = 0;

	/*
	 * Queue the work to workqueue.
	 * If the queue is full, drop the message.
	 */
	spin_lock_irqsave(&info->wq_lock, flags);
	if (info->work_write_index != info->work_read_index) {
		int index = info->work_write_index;

		memcpy(&info->work_list[index].msg, msg,
		       sizeof(struct rpmsg_s_msg));

		queue_work(info->rpmsg_wq, &info->work_list[index].work);
		info->work_write_index++;
		info->work_write_index %= WORK_MAX_NUM;
	} else {
		info->msg_drop_count[substream->stream]++;
		ret = -EPIPE;
	}
	spin_unlock_irqrestore(&info->wq_lock, flags);

	return ret;
}

static int imx_rpmsg_pcm_hw_params(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_HW_PARAM];
		msg->s_msg.header.cmd = TX_HW_PARAM;
	} else {
		msg = &info->msg[RX_HW_PARAM];
		msg->s_msg.header.cmd = RX_HW_PARAM;
	}

	msg->s_msg.param.rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		msg->s_msg.param.format   = RPMSG_S16_LE;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		msg->s_msg.param.format   = RPMSG_S24_LE;
		break;
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
		msg->s_msg.param.format   = RPMSG_DSD_U16_LE;
		break;
	case SNDRV_PCM_FORMAT_DSD_U32_LE:
		msg->s_msg.param.format   = RPMSG_DSD_U32_LE;
		break;
	default:
		msg->s_msg.param.format   = RPMSG_S32_LE;
		break;
	}

	switch (params_channels(params)) {
	case 1:
		msg->s_msg.param.channels = RPMSG_CH_LEFT;
		break;
	case 2:
		msg->s_msg.param.channels = RPMSG_CH_STEREO;
		break;
	default:
		msg->s_msg.param.channels = params_channels(params);
		break;
	}

	info->send_message(msg, info);

	return 0;
}

static snd_pcm_uframes_t imx_rpmsg_pcm_pointer(struct snd_soc_component *component,
					       struct snd_pcm_substream *substream)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;
	unsigned int pos = 0;
	int buffer_tail = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		msg = &info->msg[TX_PERIOD_DONE + MSG_TYPE_A_NUM];
	else
		msg = &info->msg[RX_PERIOD_DONE + MSG_TYPE_A_NUM];

	buffer_tail = msg->r_msg.param.buffer_tail;
	pos = buffer_tail * snd_pcm_lib_period_bytes(substream);

	return bytes_to_frames(substream->runtime, pos);
}

static void imx_rpmsg_timer_callback(struct timer_list *t)
{
	struct stream_timer  *stream_timer =
			from_timer(stream_timer, t, timer);
	struct snd_pcm_substream *substream = stream_timer->substream;
	struct rpmsg_info *info = stream_timer->info;
	struct rpmsg_msg *msg;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_PERIOD_DONE + MSG_TYPE_A_NUM];
		msg->s_msg.header.cmd = TX_PERIOD_DONE;
	} else {
		msg = &info->msg[RX_PERIOD_DONE + MSG_TYPE_A_NUM];
		msg->s_msg.header.cmd = RX_PERIOD_DONE;
	}

	imx_rpmsg_insert_workqueue(substream, msg, info);
}

static int imx_rpmsg_pcm_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;
	int ret = 0;
	int cmd;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_OPEN];
		msg->s_msg.header.cmd = TX_OPEN;

		/* reinitialize buffer counter*/
		cmd = TX_PERIOD_DONE + MSG_TYPE_A_NUM;
		info->msg[cmd].s_msg.param.buffer_tail = 0;
		info->msg[cmd].r_msg.param.buffer_tail = 0;
		info->msg[TX_POINTER].r_msg.param.buffer_offset = 0;

	} else {
		msg = &info->msg[RX_OPEN];
		msg->s_msg.header.cmd = RX_OPEN;

		/* reinitialize buffer counter*/
		cmd = RX_PERIOD_DONE + MSG_TYPE_A_NUM;
		info->msg[cmd].s_msg.param.buffer_tail = 0;
		info->msg[cmd].r_msg.param.buffer_tail = 0;
		info->msg[RX_POINTER].r_msg.param.buffer_offset = 0;
	}

	info->send_message(msg, info);

	imx_rpmsg_pcm_hardware.period_bytes_max =
			imx_rpmsg_pcm_hardware.buffer_bytes_max / 2;

	snd_soc_set_runtime_hwparams(substream, &imx_rpmsg_pcm_hardware);

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	info->msg_drop_count[substream->stream] = 0;

	/* Create timer*/
	info->stream_timer[substream->stream].info = info;
	info->stream_timer[substream->stream].substream = substream;
	timer_setup(&info->stream_timer[substream->stream].timer,
		    imx_rpmsg_timer_callback, 0);
	return ret;
}

static int imx_rpmsg_pcm_close(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;

	/* Flush work in workqueue to make TX_CLOSE is the last message */
	flush_workqueue(info->rpmsg_wq);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_CLOSE];
		msg->s_msg.header.cmd = TX_CLOSE;
	} else {
		msg = &info->msg[RX_CLOSE];
		msg->s_msg.header.cmd = RX_CLOSE;
	}

	info->send_message(msg, info);

	del_timer(&info->stream_timer[substream->stream].timer);

	rtd->dai_link->ignore_suspend = 0;

	if (info->msg_drop_count[substream->stream])
		dev_warn(rtd->dev, "Msg is dropped!, number is %d\n",
			 info->msg_drop_count[substream->stream]);

	return 0;
}

static int imx_rpmsg_pcm_prepare(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct fsl_rpmsg *rpmsg = dev_get_drvdata(cpu_dai->dev);

	/*
	 * NON-MMAP mode, NONBLOCK, Version 2, enable lpa in dts
	 * four conditions to determine the lpa is enabled.
	 */
	if ((runtime->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED ||
	     runtime->access == SNDRV_PCM_ACCESS_RW_NONINTERLEAVED) &&
	     rpmsg->enable_lpa) {
		/*
		 * Ignore suspend operation in low power mode
		 * M core will continue playback music on A core suspend.
		 */
		rtd->dai_link->ignore_suspend = 1;
		rpmsg->force_lpa = 1;
	} else {
		rpmsg->force_lpa = 0;
	}

	return 0;
}

static void imx_rpmsg_pcm_dma_complete(void *arg)
{
	struct snd_pcm_substream *substream = arg;

	snd_pcm_period_elapsed(substream);
}

static int imx_rpmsg_prepare_and_submit(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_BUFFER];
		msg->s_msg.header.cmd = TX_BUFFER;
	} else {
		msg = &info->msg[RX_BUFFER];
		msg->s_msg.header.cmd = RX_BUFFER;
	}

	/* Send buffer address and buffer size */
	msg->s_msg.param.buffer_addr = substream->runtime->dma_addr;
	msg->s_msg.param.buffer_size = snd_pcm_lib_buffer_bytes(substream);
	msg->s_msg.param.period_size = snd_pcm_lib_period_bytes(substream);
	msg->s_msg.param.buffer_tail = 0;

	info->num_period[substream->stream] = msg->s_msg.param.buffer_size /
					      msg->s_msg.param.period_size;

	info->callback[substream->stream] = imx_rpmsg_pcm_dma_complete;
	info->callback_param[substream->stream] = substream;

	return imx_rpmsg_insert_workqueue(substream, msg, info);
}

static int imx_rpmsg_async_issue_pending(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_START];
		msg->s_msg.header.cmd = TX_START;
	} else {
		msg = &info->msg[RX_START];
		msg->s_msg.header.cmd = RX_START;
	}

	return imx_rpmsg_insert_workqueue(substream, msg, info);
}

static int imx_rpmsg_restart(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_RESTART];
		msg->s_msg.header.cmd = TX_RESTART;
	} else {
		msg = &info->msg[RX_RESTART];
		msg->s_msg.header.cmd = RX_RESTART;
	}

	return imx_rpmsg_insert_workqueue(substream, msg, info);
}

static int imx_rpmsg_pause(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_PAUSE];
		msg->s_msg.header.cmd = TX_PAUSE;
	} else {
		msg = &info->msg[RX_PAUSE];
		msg->s_msg.header.cmd = RX_PAUSE;
	}

	return imx_rpmsg_insert_workqueue(substream, msg, info);
}

static int imx_rpmsg_terminate_all(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	struct rpmsg_msg *msg;
	int cmd;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_TERMINATE];
		msg->s_msg.header.cmd = TX_TERMINATE;
		/* Clear buffer count*/
		cmd = TX_PERIOD_DONE + MSG_TYPE_A_NUM;
		info->msg[cmd].s_msg.param.buffer_tail = 0;
		info->msg[cmd].r_msg.param.buffer_tail = 0;
		info->msg[TX_POINTER].r_msg.param.buffer_offset = 0;
	} else {
		msg = &info->msg[RX_TERMINATE];
		msg->s_msg.header.cmd = RX_TERMINATE;
		/* Clear buffer count*/
		cmd = RX_PERIOD_DONE + MSG_TYPE_A_NUM;
		info->msg[cmd].s_msg.param.buffer_tail = 0;
		info->msg[cmd].r_msg.param.buffer_tail = 0;
		info->msg[RX_POINTER].r_msg.param.buffer_offset = 0;
	}

	del_timer(&info->stream_timer[substream->stream].timer);

	return imx_rpmsg_insert_workqueue(substream, msg, info);
}

static int imx_rpmsg_pcm_trigger(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct fsl_rpmsg *rpmsg = dev_get_drvdata(cpu_dai->dev);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ret = imx_rpmsg_prepare_and_submit(component, substream);
		if (ret)
			return ret;
		ret = imx_rpmsg_async_issue_pending(component, substream);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		if (rpmsg->force_lpa)
			break;
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = imx_rpmsg_restart(component, substream);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (!rpmsg->force_lpa) {
			if (runtime->info & SNDRV_PCM_INFO_PAUSE)
				ret = imx_rpmsg_pause(component, substream);
			else
				ret = imx_rpmsg_terminate_all(component, substream);
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = imx_rpmsg_pause(component, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ret = imx_rpmsg_terminate_all(component, substream);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	return 0;
}

/*
 * imx_rpmsg_pcm_ack
 *
 * Send the period index to M core through rpmsg, but not send
 * all the period index to M core, reduce some unnessesary msg
 * to reduce the pressure of rpmsg bandwidth.
 */
static int imx_rpmsg_pcm_ack(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct fsl_rpmsg *rpmsg = dev_get_drvdata(cpu_dai->dev);
	struct rpmsg_info *info = dev_get_drvdata(component->dev);
	snd_pcm_uframes_t period_size = runtime->period_size;
	snd_pcm_sframes_t avail;
	struct timer_list *timer;
	struct rpmsg_msg *msg;
	unsigned long flags;
	int buffer_tail = 0;
	int written_num;

	if (!rpmsg->force_lpa)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msg = &info->msg[TX_PERIOD_DONE + MSG_TYPE_A_NUM];
		msg->s_msg.header.cmd = TX_PERIOD_DONE;
	} else {
		msg = &info->msg[RX_PERIOD_DONE + MSG_TYPE_A_NUM];
		msg->s_msg.header.cmd = RX_PERIOD_DONE;
	}

	msg->s_msg.header.type = MSG_TYPE_C;

	buffer_tail = (frames_to_bytes(runtime, runtime->control->appl_ptr) %
		       snd_pcm_lib_buffer_bytes(substream));
	buffer_tail = buffer_tail / snd_pcm_lib_period_bytes(substream);

	/* There is update for period index */
	if (buffer_tail != msg->s_msg.param.buffer_tail) {
		written_num = buffer_tail - msg->s_msg.param.buffer_tail;
		if (written_num < 0)
			written_num += runtime->periods;

		msg->s_msg.param.buffer_tail = buffer_tail;

		/* The notification message is updated to latest */
		spin_lock_irqsave(&info->lock[substream->stream], flags);
		memcpy(&info->notify[substream->stream], msg,
		       sizeof(struct rpmsg_s_msg));
		info->notify_updated[substream->stream] = true;
		spin_unlock_irqrestore(&info->lock[substream->stream], flags);

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			avail = snd_pcm_playback_hw_avail(runtime);
		else
			avail = snd_pcm_capture_hw_avail(runtime);

		timer = &info->stream_timer[substream->stream].timer;
		/*
		 * If the data in the buffer is less than one period before
		 * this fill, which means the data may not enough on M
		 * core side, we need to send message immediately to let
		 * M core know the pointer is updated.
		 * if there is more than one period data in the buffer before
		 * this fill, which means the data is enough on M core side,
		 * we can delay one period (using timer) to send the message
		 * for reduce the message number in workqueue, because the
		 * pointer may be updated by ack function later, we can
		 * send latest pointer to M core side.
		 */
		if ((avail - written_num * period_size) <= period_size) {
			imx_rpmsg_insert_workqueue(substream, msg, info);
		} else if (rpmsg->force_lpa && !timer_pending(timer)) {
			int time_msec;

			time_msec = (int)(runtime->period_size * 1000 / runtime->rate);
			mod_timer(timer, jiffies + msecs_to_jiffies(time_msec));
		}
	}

	return 0;
}

static int imx_rpmsg_pcm_new(struct snd_soc_component *component,
			     struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct fsl_rpmsg *rpmsg = dev_get_drvdata(cpu_dai->dev);
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	imx_rpmsg_pcm_hardware.buffer_bytes_max = rpmsg->buffer_size;
	return snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV_WC,
					    pcm->card->dev, rpmsg->buffer_size);
}

static const struct snd_soc_component_driver imx_rpmsg_soc_component = {
	.name		= IMX_PCM_DRV_NAME,
	.pcm_construct	= imx_rpmsg_pcm_new,
	.open		= imx_rpmsg_pcm_open,
	.close		= imx_rpmsg_pcm_close,
	.hw_params	= imx_rpmsg_pcm_hw_params,
	.trigger	= imx_rpmsg_pcm_trigger,
	.pointer	= imx_rpmsg_pcm_pointer,
	.ack		= imx_rpmsg_pcm_ack,
	.prepare	= imx_rpmsg_pcm_prepare,
};

static void imx_rpmsg_pcm_work(struct work_struct *work)
{
	struct work_of_rpmsg *work_of_rpmsg;
	bool is_notification = false;
	struct rpmsg_info *info;
	struct rpmsg_msg msg;
	unsigned long flags;

	work_of_rpmsg = container_of(work, struct work_of_rpmsg, work);
	info = work_of_rpmsg->info;

	/*
	 * Every work in the work queue, first we check if there
	 * is update for period is filled, because there may be not
	 * enough data in M core side, need to let M core know
	 * data is updated immediately.
	 */
	spin_lock_irqsave(&info->lock[TX], flags);
	if (info->notify_updated[TX]) {
		memcpy(&msg, &info->notify[TX], sizeof(struct rpmsg_s_msg));
		info->notify_updated[TX] = false;
		spin_unlock_irqrestore(&info->lock[TX], flags);
		info->send_message(&msg, info);
	} else {
		spin_unlock_irqrestore(&info->lock[TX], flags);
	}

	spin_lock_irqsave(&info->lock[RX], flags);
	if (info->notify_updated[RX]) {
		memcpy(&msg, &info->notify[RX], sizeof(struct rpmsg_s_msg));
		info->notify_updated[RX] = false;
		spin_unlock_irqrestore(&info->lock[RX], flags);
		info->send_message(&msg, info);
	} else {
		spin_unlock_irqrestore(&info->lock[RX], flags);
	}

	/* Skip the notification message for it has been processed above */
	if (work_of_rpmsg->msg.s_msg.header.type == MSG_TYPE_C &&
	    (work_of_rpmsg->msg.s_msg.header.cmd == TX_PERIOD_DONE ||
	     work_of_rpmsg->msg.s_msg.header.cmd == RX_PERIOD_DONE))
		is_notification = true;

	if (!is_notification)
		info->send_message(&work_of_rpmsg->msg, info);

	/* update read index */
	spin_lock_irqsave(&info->wq_lock, flags);
	info->work_read_index++;
	info->work_read_index %= WORK_MAX_NUM;
	spin_unlock_irqrestore(&info->wq_lock, flags);
}

static int imx_rpmsg_pcm_probe(struct platform_device *pdev)
{
	struct snd_soc_component *component;
	struct rpmsg_info *info;
	int ret, i;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);

	info->rpdev = container_of(pdev->dev.parent, struct rpmsg_device, dev);
	info->dev = &pdev->dev;
	/* Setup work queue */
	info->rpmsg_wq = alloc_ordered_workqueue(info->rpdev->id.name,
						 WQ_HIGHPRI |
						 WQ_UNBOUND |
						 WQ_FREEZABLE);
	if (!info->rpmsg_wq) {
		dev_err(&pdev->dev, "workqueue create failed\n");
		return -ENOMEM;
	}

	/* Write index initialize 1, make it differ with the read index */
	info->work_write_index = 1;
	info->send_message = imx_rpmsg_pcm_send_message;

	for (i = 0; i < WORK_MAX_NUM; i++) {
		INIT_WORK(&info->work_list[i].work, imx_rpmsg_pcm_work);
		info->work_list[i].info = info;
	}

	/* Initialize msg */
	for (i = 0; i < MSG_MAX_NUM; i++) {
		info->msg[i].s_msg.header.cate  = IMX_RPMSG_AUDIO;
		info->msg[i].s_msg.header.major = IMX_RMPSG_MAJOR;
		info->msg[i].s_msg.header.minor = IMX_RMPSG_MINOR;
		info->msg[i].s_msg.header.type  = MSG_TYPE_A;
		info->msg[i].s_msg.param.audioindex = 0;
	}

	init_completion(&info->cmd_complete);
	mutex_init(&info->msg_lock);
	spin_lock_init(&info->lock[TX]);
	spin_lock_init(&info->lock[RX]);
	spin_lock_init(&info->wq_lock);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &imx_rpmsg_soc_component,
					      NULL, 0);
	if (ret)
		goto fail;

	component = snd_soc_lookup_component(&pdev->dev, NULL);
	if (!component) {
		ret = -EINVAL;
		goto fail;
	}

	/* platform component name is used by machine driver to link with */
	component->name = info->rpdev->id.name;

#ifdef CONFIG_DEBUG_FS
	component->debugfs_prefix = "rpmsg";
#endif

	return 0;

fail:
	if (info->rpmsg_wq)
		destroy_workqueue(info->rpmsg_wq);

	return ret;
}

static int imx_rpmsg_pcm_remove(struct platform_device *pdev)
{
	struct rpmsg_info *info = platform_get_drvdata(pdev);

	if (info->rpmsg_wq)
		destroy_workqueue(info->rpmsg_wq);

	return 0;
}

#ifdef CONFIG_PM
static int imx_rpmsg_pcm_runtime_resume(struct device *dev)
{
	struct rpmsg_info *info = dev_get_drvdata(dev);

	cpu_latency_qos_add_request(&info->pm_qos_req, 0);

	return 0;
}

static int imx_rpmsg_pcm_runtime_suspend(struct device *dev)
{
	struct rpmsg_info *info = dev_get_drvdata(dev);

	cpu_latency_qos_remove_request(&info->pm_qos_req);

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int imx_rpmsg_pcm_suspend(struct device *dev)
{
	struct rpmsg_info *info = dev_get_drvdata(dev);
	struct rpmsg_msg *rpmsg_tx;
	struct rpmsg_msg *rpmsg_rx;

	rpmsg_tx = &info->msg[TX_SUSPEND];
	rpmsg_rx = &info->msg[RX_SUSPEND];

	rpmsg_tx->s_msg.header.cmd = TX_SUSPEND;
	info->send_message(rpmsg_tx, info);

	rpmsg_rx->s_msg.header.cmd = RX_SUSPEND;
	info->send_message(rpmsg_rx, info);

	return 0;
}

static int imx_rpmsg_pcm_resume(struct device *dev)
{
	struct rpmsg_info *info = dev_get_drvdata(dev);
	struct rpmsg_msg *rpmsg_tx;
	struct rpmsg_msg *rpmsg_rx;

	rpmsg_tx = &info->msg[TX_RESUME];
	rpmsg_rx = &info->msg[RX_RESUME];

	rpmsg_tx->s_msg.header.cmd = TX_RESUME;
	info->send_message(rpmsg_tx, info);

	rpmsg_rx->s_msg.header.cmd = RX_RESUME;
	info->send_message(rpmsg_rx, info);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops imx_rpmsg_pcm_pm_ops = {
	SET_RUNTIME_PM_OPS(imx_rpmsg_pcm_runtime_suspend,
			   imx_rpmsg_pcm_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(imx_rpmsg_pcm_suspend,
				imx_rpmsg_pcm_resume)
};

static struct platform_driver imx_pcm_rpmsg_driver = {
	.probe  = imx_rpmsg_pcm_probe,
	.remove	= imx_rpmsg_pcm_remove,
	.driver = {
		.name = IMX_PCM_DRV_NAME,
		.pm = &imx_rpmsg_pcm_pm_ops,
	},
};
module_platform_driver(imx_pcm_rpmsg_driver);

MODULE_DESCRIPTION("Freescale SoC Audio RPMSG PCM interface");
MODULE_AUTHOR("Shengjiu Wang <shengjiu.wang@nxp.com>");
MODULE_ALIAS("platform:" IMX_PCM_DRV_NAME);
MODULE_LICENSE("GPL v2");
