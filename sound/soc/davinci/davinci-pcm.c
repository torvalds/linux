/*
 * ALSA PCM interface for the TI DAVINCI processor
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 * added SRAM ping/pong (C) 2008 Troy Kisky <troy.kisky@boundarydevices.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/dma.h>
#include <mach/edma.h>
#include <mach/sram.h>

#include "davinci-pcm.h"

#ifdef DEBUG
static void print_buf_info(int slot, char *name)
{
	struct edmacc_param p;
	if (slot < 0)
		return;
	edma_read_slot(slot, &p);
	printk(KERN_DEBUG "%s: 0x%x, opt=%x, src=%x, a_b_cnt=%x dst=%x\n",
			name, slot, p.opt, p.src, p.a_b_cnt, p.dst);
	printk(KERN_DEBUG "    src_dst_bidx=%x link_bcntrld=%x src_dst_cidx=%x ccnt=%x\n",
			p.src_dst_bidx, p.link_bcntrld, p.src_dst_cidx, p.ccnt);
}
#else
static void print_buf_info(int slot, char *name)
{
}
#endif

#define DAVINCI_PCM_FMTBITS	(\
				SNDRV_PCM_FMTBIT_S8	|\
				SNDRV_PCM_FMTBIT_U8	|\
				SNDRV_PCM_FMTBIT_S16_LE	|\
				SNDRV_PCM_FMTBIT_S16_BE	|\
				SNDRV_PCM_FMTBIT_U16_LE	|\
				SNDRV_PCM_FMTBIT_U16_BE	|\
				SNDRV_PCM_FMTBIT_S24_LE	|\
				SNDRV_PCM_FMTBIT_S24_BE	|\
				SNDRV_PCM_FMTBIT_U24_LE	|\
				SNDRV_PCM_FMTBIT_U24_BE	|\
				SNDRV_PCM_FMTBIT_S32_LE	|\
				SNDRV_PCM_FMTBIT_S32_BE	|\
				SNDRV_PCM_FMTBIT_U32_LE	|\
				SNDRV_PCM_FMTBIT_U32_BE)

static struct snd_pcm_hardware pcm_hardware_playback = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME|
		 SNDRV_PCM_INFO_BATCH),
	.formats = DAVINCI_PCM_FMTBITS,
	.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
		  SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |
		  SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
		  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
		  SNDRV_PCM_RATE_KNOT),
	.rate_min = 8000,
	.rate_max = 96000,
	.channels_min = 2,
	.channels_max = 384,
	.buffer_bytes_max = 128 * 1024,
	.period_bytes_min = 32,
	.period_bytes_max = 8 * 1024,
	.periods_min = 16,
	.periods_max = 255,
	.fifo_size = 0,
};

static struct snd_pcm_hardware pcm_hardware_capture = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE |
		 SNDRV_PCM_INFO_BATCH),
	.formats = DAVINCI_PCM_FMTBITS,
	.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
		  SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |
		  SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
		  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
		  SNDRV_PCM_RATE_KNOT),
	.rate_min = 8000,
	.rate_max = 96000,
	.channels_min = 2,
	.channels_max = 384,
	.buffer_bytes_max = 128 * 1024,
	.period_bytes_min = 32,
	.period_bytes_max = 8 * 1024,
	.periods_min = 16,
	.periods_max = 255,
	.fifo_size = 0,
};

/*
 * How ping/pong works....
 *
 * Playback:
 * ram_params - copys 2*ping_size from start of SDRAM to iram,
 * 	links to ram_link2
 * ram_link2 - copys rest of SDRAM to iram in ping_size units,
 * 	links to ram_link
 * ram_link - copys entire SDRAM to iram in ping_size uints,
 * 	links to self
 *
 * asp_params - same as asp_link[0]
 * asp_link[0] - copys from lower half of iram to asp port
 * 	links to asp_link[1], triggers iram copy event on completion
 * asp_link[1] - copys from upper half of iram to asp port
 * 	links to asp_link[0], triggers iram copy event on completion
 * 	triggers interrupt only needed to let upper SOC levels update position
 * 	in stream on completion
 *
 * When playback is started:
 * 	ram_params started
 * 	asp_params started
 *
 * Capture:
 * ram_params - same as ram_link,
 * 	links to ram_link
 * ram_link - same as playback
 * 	links to self
 *
 * asp_params - same as playback
 * asp_link[0] - same as playback
 * asp_link[1] - same as playback
 *
 * When capture is started:
 * 	asp_params started
 */
struct davinci_runtime_data {
	spinlock_t lock;
	int period;		/* current DMA period */
	int asp_channel;	/* Master DMA channel */
	int asp_link[2];	/* asp parameter link channel, ping/pong */
	struct davinci_pcm_dma_params *params;	/* DMA params */
	int ram_channel;
	int ram_link;
	int ram_link2;
	struct edmacc_param asp_params;
	struct edmacc_param ram_params;
};

static void davinci_pcm_period_elapsed(struct snd_pcm_substream *substream)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;

	prtd->period++;
	if (unlikely(prtd->period >= runtime->periods))
		prtd->period = 0;
}

static void davinci_pcm_period_reset(struct snd_pcm_substream *substream)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;

	prtd->period = 0;
}
/*
 * Not used with ping/pong
 */
static void davinci_pcm_enqueue_dma(struct snd_pcm_substream *substream)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int period_size;
	unsigned int dma_offset;
	dma_addr_t dma_pos;
	dma_addr_t src, dst;
	unsigned short src_bidx, dst_bidx;
	unsigned short src_cidx, dst_cidx;
	unsigned int data_type;
	unsigned short acnt;
	unsigned int count;
	unsigned int fifo_level;

	period_size = snd_pcm_lib_period_bytes(substream);
	dma_offset = prtd->period * period_size;
	dma_pos = runtime->dma_addr + dma_offset;
	fifo_level = prtd->params->fifo_level;

	pr_debug("davinci_pcm: audio_set_dma_params_play channel = %d "
		"dma_ptr = %x period_size=%x\n", prtd->asp_link[0], dma_pos,
		period_size);

	data_type = prtd->params->data_type;
	count = period_size / data_type;
	if (fifo_level)
		count /= fifo_level;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		src = dma_pos;
		dst = prtd->params->dma_addr;
		src_bidx = data_type;
		dst_bidx = 0;
		src_cidx = data_type * fifo_level;
		dst_cidx = 0;
	} else {
		src = prtd->params->dma_addr;
		dst = dma_pos;
		src_bidx = 0;
		dst_bidx = data_type;
		src_cidx = 0;
		dst_cidx = data_type * fifo_level;
	}

	acnt = prtd->params->acnt;
	edma_set_src(prtd->asp_link[0], src, INCR, W8BIT);
	edma_set_dest(prtd->asp_link[0], dst, INCR, W8BIT);

	edma_set_src_index(prtd->asp_link[0], src_bidx, src_cidx);
	edma_set_dest_index(prtd->asp_link[0], dst_bidx, dst_cidx);

	if (!fifo_level)
		edma_set_transfer_params(prtd->asp_link[0], acnt, count, 1, 0,
							ASYNC);
	else
		edma_set_transfer_params(prtd->asp_link[0], acnt, fifo_level,
							count, fifo_level,
							ABSYNC);
}

static void davinci_pcm_dma_irq(unsigned link, u16 ch_status, void *data)
{
	struct snd_pcm_substream *substream = data;
	struct davinci_runtime_data *prtd = substream->runtime->private_data;

	print_buf_info(prtd->ram_channel, "i ram_channel");
	pr_debug("davinci_pcm: link=%d, status=0x%x\n", link, ch_status);

	if (unlikely(ch_status != DMA_COMPLETE))
		return;

	if (snd_pcm_running(substream)) {
		spin_lock(&prtd->lock);
		if (prtd->ram_channel < 0) {
			/* No ping/pong must fix up link dma data*/
			davinci_pcm_enqueue_dma(substream);
		}
		davinci_pcm_period_elapsed(substream);
		spin_unlock(&prtd->lock);
		snd_pcm_period_elapsed(substream);
	}
}

static int allocate_sram(struct snd_pcm_substream *substream, unsigned size,
		struct snd_pcm_hardware *ppcm)
{
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	struct snd_dma_buffer *iram_dma = NULL;
	dma_addr_t iram_phys = 0;
	void *iram_virt = NULL;

	if (buf->private_data || !size)
		return 0;

	ppcm->period_bytes_max = size;
	iram_virt = sram_alloc(size, &iram_phys);
	if (!iram_virt)
		goto exit1;
	iram_dma = kzalloc(sizeof(*iram_dma), GFP_KERNEL);
	if (!iram_dma)
		goto exit2;
	iram_dma->area = iram_virt;
	iram_dma->addr = iram_phys;
	memset(iram_dma->area, 0, size);
	iram_dma->bytes = size;
	buf->private_data = iram_dma;
	return 0;
exit2:
	if (iram_virt)
		sram_free(iram_virt, size);
exit1:
	return -ENOMEM;
}

/*
 * Only used with ping/pong.
 * This is called after runtime->dma_addr, period_bytes and data_type are valid
 */
static int ping_pong_dma_setup(struct snd_pcm_substream *substream)
{
	unsigned short ram_src_cidx, ram_dst_cidx;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct davinci_runtime_data *prtd = runtime->private_data;
	struct snd_dma_buffer *iram_dma =
		(struct snd_dma_buffer *)substream->dma_buffer.private_data;
	struct davinci_pcm_dma_params *params = prtd->params;
	unsigned int data_type = params->data_type;
	unsigned int acnt = params->acnt;
	/* divide by 2 for ping/pong */
	unsigned int ping_size = snd_pcm_lib_period_bytes(substream) >> 1;
	unsigned int fifo_level = prtd->params->fifo_level;
	unsigned int count;
	if ((data_type == 0) || (data_type > 4)) {
		printk(KERN_ERR "%s: data_type=%i\n", __func__, data_type);
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_addr_t asp_src_pong = iram_dma->addr + ping_size;
		ram_src_cidx = ping_size;
		ram_dst_cidx = -ping_size;
		edma_set_src(prtd->asp_link[1], asp_src_pong, INCR, W8BIT);

		edma_set_src_index(prtd->asp_link[0], data_type,
				data_type * fifo_level);
		edma_set_src_index(prtd->asp_link[1], data_type,
				data_type * fifo_level);

		edma_set_src(prtd->ram_link, runtime->dma_addr, INCR, W32BIT);
	} else {
		dma_addr_t asp_dst_pong = iram_dma->addr + ping_size;
		ram_src_cidx = -ping_size;
		ram_dst_cidx = ping_size;
		edma_set_dest(prtd->asp_link[1], asp_dst_pong, INCR, W8BIT);

		edma_set_dest_index(prtd->asp_link[0], data_type,
				data_type * fifo_level);
		edma_set_dest_index(prtd->asp_link[1], data_type,
				data_type * fifo_level);

		edma_set_dest(prtd->ram_link, runtime->dma_addr, INCR, W32BIT);
	}

	if (!fifo_level) {
		count = ping_size / data_type;
		edma_set_transfer_params(prtd->asp_link[0], acnt, count,
				1, 0, ASYNC);
		edma_set_transfer_params(prtd->asp_link[1], acnt, count,
				1, 0, ASYNC);
	} else {
		count = ping_size / (data_type * fifo_level);
		edma_set_transfer_params(prtd->asp_link[0], acnt, fifo_level,
				count, fifo_level, ABSYNC);
		edma_set_transfer_params(prtd->asp_link[1], acnt, fifo_level,
				count, fifo_level, ABSYNC);
	}

	edma_set_src_index(prtd->ram_link, ping_size, ram_src_cidx);
	edma_set_dest_index(prtd->ram_link, ping_size, ram_dst_cidx);
	edma_set_transfer_params(prtd->ram_link, ping_size, 2,
			runtime->periods, 2, ASYNC);

	/* init master params */
	edma_read_slot(prtd->asp_link[0], &prtd->asp_params);
	edma_read_slot(prtd->ram_link, &prtd->ram_params);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		struct edmacc_param p_ram;
		/* Copy entire iram buffer before playback started */
		prtd->ram_params.a_b_cnt = (1 << 16) | (ping_size << 1);
		/* 0 dst_bidx */
		prtd->ram_params.src_dst_bidx = (ping_size << 1);
		/* 0 dst_cidx */
		prtd->ram_params.src_dst_cidx = (ping_size << 1);
		prtd->ram_params.ccnt = 1;

		/* Skip 1st period */
		edma_read_slot(prtd->ram_link, &p_ram);
		p_ram.src += (ping_size << 1);
		p_ram.ccnt -= 1;
		edma_write_slot(prtd->ram_link2, &p_ram);
		/*
		 * When 1st started, ram -> iram dma channel will fill the
		 * entire iram.  Then, whenever a ping/pong asp buffer finishes,
		 * 1/2 iram will be filled.
		 */
		prtd->ram_params.link_bcntrld =
			EDMA_CHAN_SLOT(prtd->ram_link2) << 5;
	}
	return 0;
}

/* 1 asp tx or rx channel using 2 parameter channels
 * 1 ram to/from iram channel using 1 parameter channel
 *
 * Playback
 * ram copy channel kicks off first,
 * 1st ram copy of entire iram buffer completion kicks off asp channel
 * asp tcc always kicks off ram copy of 1/2 iram buffer
 *
 * Record
 * asp channel starts, tcc kicks off ram copy
 */
static int request_ping_pong(struct snd_pcm_substream *substream,
		struct davinci_runtime_data *prtd,
		struct snd_dma_buffer *iram_dma)
{
	dma_addr_t asp_src_ping;
	dma_addr_t asp_dst_ping;
	int ret;
	struct davinci_pcm_dma_params *params = prtd->params;

	/* Request ram master channel */
	ret = prtd->ram_channel = edma_alloc_channel(EDMA_CHANNEL_ANY,
				  davinci_pcm_dma_irq, substream,
				  prtd->params->ram_chan_q);
	if (ret < 0)
		goto exit1;

	/* Request ram link channel */
	ret = prtd->ram_link = edma_alloc_slot(
			EDMA_CTLR(prtd->ram_channel), EDMA_SLOT_ANY);
	if (ret < 0)
		goto exit2;

	ret = prtd->asp_link[1] = edma_alloc_slot(
			EDMA_CTLR(prtd->asp_channel), EDMA_SLOT_ANY);
	if (ret < 0)
		goto exit3;

	prtd->ram_link2 = -1;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = prtd->ram_link2 = edma_alloc_slot(
			EDMA_CTLR(prtd->ram_channel), EDMA_SLOT_ANY);
		if (ret < 0)
			goto exit4;
	}
	/* circle ping-pong buffers */
	edma_link(prtd->asp_link[0], prtd->asp_link[1]);
	edma_link(prtd->asp_link[1], prtd->asp_link[0]);
	/* circle ram buffers */
	edma_link(prtd->ram_link, prtd->ram_link);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		asp_src_ping = iram_dma->addr;
		asp_dst_ping = params->dma_addr;	/* fifo */
	} else {
		asp_src_ping = params->dma_addr;	/* fifo */
		asp_dst_ping = iram_dma->addr;
	}
	/* ping */
	edma_set_src(prtd->asp_link[0], asp_src_ping, INCR, W16BIT);
	edma_set_dest(prtd->asp_link[0], asp_dst_ping, INCR, W16BIT);
	edma_set_src_index(prtd->asp_link[0], 0, 0);
	edma_set_dest_index(prtd->asp_link[0], 0, 0);

	edma_read_slot(prtd->asp_link[0], &prtd->asp_params);
	prtd->asp_params.opt &= ~(TCCMODE | EDMA_TCC(0x3f) | TCINTEN);
	prtd->asp_params.opt |= TCCHEN |
		EDMA_TCC(prtd->ram_channel & 0x3f);
	edma_write_slot(prtd->asp_link[0], &prtd->asp_params);

	/* pong */
	edma_set_src(prtd->asp_link[1], asp_src_ping, INCR, W16BIT);
	edma_set_dest(prtd->asp_link[1], asp_dst_ping, INCR, W16BIT);
	edma_set_src_index(prtd->asp_link[1], 0, 0);
	edma_set_dest_index(prtd->asp_link[1], 0, 0);

	edma_read_slot(prtd->asp_link[1], &prtd->asp_params);
	prtd->asp_params.opt &= ~(TCCMODE | EDMA_TCC(0x3f));
	/* interrupt after every pong completion */
	prtd->asp_params.opt |= TCINTEN | TCCHEN |
		EDMA_TCC(prtd->ram_channel & 0x3f);
	edma_write_slot(prtd->asp_link[1], &prtd->asp_params);

	/* ram */
	edma_set_src(prtd->ram_link, iram_dma->addr, INCR, W32BIT);
	edma_set_dest(prtd->ram_link, iram_dma->addr, INCR, W32BIT);
	pr_debug("%s: audio dma channels/slots in use for ram:%u %u %u,"
		"for asp:%u %u %u\n", __func__,
		prtd->ram_channel, prtd->ram_link, prtd->ram_link2,
		prtd->asp_channel, prtd->asp_link[0],
		prtd->asp_link[1]);
	return 0;
exit4:
	edma_free_channel(prtd->asp_link[1]);
	prtd->asp_link[1] = -1;
exit3:
	edma_free_channel(prtd->ram_link);
	prtd->ram_link = -1;
exit2:
	edma_free_channel(prtd->ram_channel);
	prtd->ram_channel = -1;
exit1:
	return ret;
}

static int davinci_pcm_dma_request(struct snd_pcm_substream *substream)
{
	struct snd_dma_buffer *iram_dma;
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	struct davinci_pcm_dma_params *params = prtd->params;
	int ret;

	if (!params)
		return -ENODEV;

	/* Request asp master DMA channel */
	ret = prtd->asp_channel = edma_alloc_channel(params->channel,
			davinci_pcm_dma_irq, substream,
			prtd->params->asp_chan_q);
	if (ret < 0)
		goto exit1;

	/* Request asp link channels */
	ret = prtd->asp_link[0] = edma_alloc_slot(
			EDMA_CTLR(prtd->asp_channel), EDMA_SLOT_ANY);
	if (ret < 0)
		goto exit2;

	iram_dma = (struct snd_dma_buffer *)substream->dma_buffer.private_data;
	if (iram_dma) {
		if (request_ping_pong(substream, prtd, iram_dma) == 0)
			return 0;
		printk(KERN_WARNING "%s: dma channel allocation failed,"
				"not using sram\n", __func__);
	}

	/* Issue transfer completion IRQ when the channel completes a
	 * transfer, then always reload from the same slot (by a kind
	 * of loopback link).  The completion IRQ handler will update
	 * the reload slot with a new buffer.
	 *
	 * REVISIT save p_ram here after setting up everything except
	 * the buffer and its length (ccnt) ... use it as a template
	 * so davinci_pcm_enqueue_dma() takes less time in IRQ.
	 */
	edma_read_slot(prtd->asp_link[0], &prtd->asp_params);
	prtd->asp_params.opt |= TCINTEN |
		EDMA_TCC(EDMA_CHAN_SLOT(prtd->asp_channel));
	prtd->asp_params.link_bcntrld = EDMA_CHAN_SLOT(prtd->asp_link[0]) << 5;
	edma_write_slot(prtd->asp_link[0], &prtd->asp_params);
	return 0;
exit2:
	edma_free_channel(prtd->asp_channel);
	prtd->asp_channel = -1;
exit1:
	return ret;
}

static int davinci_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	spin_lock(&prtd->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		edma_start(prtd->asp_channel);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
		    prtd->ram_channel >= 0) {
			/* copy 1st iram buffer */
			edma_start(prtd->ram_channel);
		}
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		edma_resume(prtd->asp_channel);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		edma_pause(prtd->asp_channel);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock(&prtd->lock);

	return ret;
}

static int davinci_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;

	davinci_pcm_period_reset(substream);
	if (prtd->ram_channel >= 0) {
		int ret = ping_pong_dma_setup(substream);
		if (ret < 0)
			return ret;

		edma_write_slot(prtd->ram_channel, &prtd->ram_params);
		edma_write_slot(prtd->asp_channel, &prtd->asp_params);

		print_buf_info(prtd->ram_channel, "ram_channel");
		print_buf_info(prtd->ram_link, "ram_link");
		print_buf_info(prtd->ram_link2, "ram_link2");
		print_buf_info(prtd->asp_channel, "asp_channel");
		print_buf_info(prtd->asp_link[0], "asp_link[0]");
		print_buf_info(prtd->asp_link[1], "asp_link[1]");

		/*
		 * There is a phase offset of 2 periods between the position
		 * used by dma setup and the position reported in the pointer
		 * function.
		 *
		 * The phase offset, when not using ping-pong buffers, is due to
		 * the two consecutive calls to davinci_pcm_enqueue_dma() below.
		 *
		 * Whereas here, with ping-pong buffers, the phase is due to
		 * there being an entire buffer transfer complete before the
		 * first dma completion event triggers davinci_pcm_dma_irq().
		 */
		davinci_pcm_period_elapsed(substream);
		davinci_pcm_period_elapsed(substream);

		return 0;
	}
	davinci_pcm_enqueue_dma(substream);
	davinci_pcm_period_elapsed(substream);

	/* Copy self-linked parameter RAM entry into master channel */
	edma_read_slot(prtd->asp_link[0], &prtd->asp_params);
	edma_write_slot(prtd->asp_channel, &prtd->asp_params);
	davinci_pcm_enqueue_dma(substream);
	davinci_pcm_period_elapsed(substream);

	return 0;
}

static snd_pcm_uframes_t
davinci_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct davinci_runtime_data *prtd = runtime->private_data;
	unsigned int offset;
	int asp_count;
	unsigned int period_size = snd_pcm_lib_period_bytes(substream);

	/*
	 * There is a phase offset of 2 periods between the position used by dma
	 * setup and the position reported in the pointer function. Either +2 in
	 * the dma setup or -2 here in the pointer function (with wrapping,
	 * both) accounts for this offset -- choose the latter since it makes
	 * the first-time setup clearer.
	 */
	spin_lock(&prtd->lock);
	asp_count = prtd->period - 2;
	spin_unlock(&prtd->lock);

	if (asp_count < 0)
		asp_count += runtime->periods;
	asp_count *= period_size;

	offset = bytes_to_frames(runtime, asp_count);
	if (offset >= runtime->buffer_size)
		offset = 0;

	return offset;
}

static int davinci_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct davinci_runtime_data *prtd;
	struct snd_pcm_hardware *ppcm;
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct davinci_pcm_dma_params *pa;
	struct davinci_pcm_dma_params *params;

	pa = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	if (!pa)
		return -ENODEV;
	params = &pa[substream->stream];

	ppcm = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			&pcm_hardware_playback : &pcm_hardware_capture;
	allocate_sram(substream, params->sram_size, ppcm);
	snd_soc_set_runtime_hwparams(substream, ppcm);
	/* ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	prtd = kzalloc(sizeof(struct davinci_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);
	prtd->params = params;
	prtd->asp_channel = -1;
	prtd->asp_link[0] = prtd->asp_link[1] = -1;
	prtd->ram_channel = -1;
	prtd->ram_link = -1;
	prtd->ram_link2 = -1;

	runtime->private_data = prtd;

	ret = davinci_pcm_dma_request(substream);
	if (ret) {
		printk(KERN_ERR "davinci_pcm: Failed to get dma channels\n");
		kfree(prtd);
	}

	return ret;
}

static int davinci_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct davinci_runtime_data *prtd = runtime->private_data;

	if (prtd->ram_channel >= 0)
		edma_stop(prtd->ram_channel);
	if (prtd->asp_channel >= 0)
		edma_stop(prtd->asp_channel);
	if (prtd->asp_link[0] >= 0)
		edma_unlink(prtd->asp_link[0]);
	if (prtd->asp_link[1] >= 0)
		edma_unlink(prtd->asp_link[1]);
	if (prtd->ram_link >= 0)
		edma_unlink(prtd->ram_link);

	if (prtd->asp_link[0] >= 0)
		edma_free_slot(prtd->asp_link[0]);
	if (prtd->asp_link[1] >= 0)
		edma_free_slot(prtd->asp_link[1]);
	if (prtd->asp_channel >= 0)
		edma_free_channel(prtd->asp_channel);
	if (prtd->ram_link >= 0)
		edma_free_slot(prtd->ram_link);
	if (prtd->ram_link2 >= 0)
		edma_free_slot(prtd->ram_link2);
	if (prtd->ram_channel >= 0)
		edma_free_channel(prtd->ram_channel);

	kfree(prtd);

	return 0;
}

static int davinci_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int davinci_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int davinci_pcm_mmap(struct snd_pcm_substream *substream,
			    struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops davinci_pcm_ops = {
	.open = 	davinci_pcm_open,
	.close = 	davinci_pcm_close,
	.ioctl = 	snd_pcm_lib_ioctl,
	.hw_params = 	davinci_pcm_hw_params,
	.hw_free = 	davinci_pcm_hw_free,
	.prepare = 	davinci_pcm_prepare,
	.trigger = 	davinci_pcm_trigger,
	.pointer = 	davinci_pcm_pointer,
	.mmap = 	davinci_pcm_mmap,
};

static int davinci_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream,
		size_t size)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);

	pr_debug("davinci_pcm: preallocate_dma_buffer: area=%p, addr=%p, "
		"size=%d\n", (void *) buf->area, (void *) buf->addr, size);

	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

static void davinci_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		struct snd_dma_buffer *iram_dma;
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_writecombine(pcm->card->dev, buf->bytes,
				      buf->area, buf->addr);
		buf->area = NULL;
		iram_dma = buf->private_data;
		if (iram_dma) {
			sram_free(iram_dma->area, iram_dma->bytes);
			kfree(iram_dma);
		}
	}
}

static u64 davinci_pcm_dmamask = DMA_BIT_MASK(32);

static int davinci_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &davinci_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = davinci_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK,
			pcm_hardware_playback.buffer_bytes_max);
		if (ret)
			return ret;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = davinci_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE,
			pcm_hardware_capture.buffer_bytes_max);
		if (ret)
			return ret;
	}

	return 0;
}

static struct snd_soc_platform_driver davinci_soc_platform = {
	.ops =		&davinci_pcm_ops,
	.pcm_new = 	davinci_pcm_new,
	.pcm_free = 	davinci_pcm_free,
};

static int __devinit davinci_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &davinci_soc_platform);
}

static int __devexit davinci_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver davinci_pcm_driver = {
	.driver = {
			.name = "davinci-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = davinci_soc_platform_probe,
	.remove = __devexit_p(davinci_soc_platform_remove),
};

module_platform_driver(davinci_pcm_driver);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI PCM DMA module");
MODULE_LICENSE("GPL");
