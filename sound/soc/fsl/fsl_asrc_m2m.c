// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2014-2016 Freescale Semiconductor, Inc.
// Copyright (C) 2019-2024 NXP
//
// Freescale ASRC Memory to Memory (M2M) driver

#include <linux/dma/imx-dma.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <sound/asound.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>

#include "fsl_asrc_common.h"

#define DIR_STR(dir) (dir) == IN ? "in" : "out"

#define ASRC_xPUT_DMA_CALLBACK(dir) \
	(((dir) == IN) ? asrc_input_dma_callback \
	: asrc_output_dma_callback)

/* Maximum output and capture buffer size */
#define ASRC_M2M_BUFFER_SIZE (512 * 1024)

/* Maximum output and capture period size */
#define ASRC_M2M_PERIOD_SIZE (48 * 1024)

/* dma complete callback */
static void asrc_input_dma_callback(void *data)
{
	struct fsl_asrc_pair *pair = (struct fsl_asrc_pair *)data;

	complete(&pair->complete[IN]);
}

/* dma complete callback */
static void asrc_output_dma_callback(void *data)
{
	struct fsl_asrc_pair *pair = (struct fsl_asrc_pair *)data;

	complete(&pair->complete[OUT]);
}

/**
 *asrc_read_last_fifo: read all the remaining data from FIFO
 *@pair: Structure pointer of fsl_asrc_pair
 *@dma_vaddr: virtual address of capture buffer
 *@length: payload length of capture buffer
 */
static void asrc_read_last_fifo(struct fsl_asrc_pair *pair, void *dma_vaddr, u32 *length)
{
	struct fsl_asrc *asrc = pair->asrc;
	enum asrc_pair_index index = pair->index;
	u32 i, reg, size, t_size = 0, width;
	u32 *reg32 = NULL;
	u16 *reg16 = NULL;
	u8  *reg24 = NULL;

	width = snd_pcm_format_physical_width(pair->sample_format[OUT]);
	if (width == 32)
		reg32 = dma_vaddr + *length;
	else if (width == 16)
		reg16 = dma_vaddr + *length;
	else
		reg24 = dma_vaddr + *length;
retry:
	size = asrc->get_output_fifo_size(pair);
	if (size + *length > ASRC_M2M_BUFFER_SIZE)
		goto end;

	for (i = 0; i < size * pair->channels; i++) {
		regmap_read(asrc->regmap, asrc->get_fifo_addr(OUT, index), &reg);
		if (reg32) {
			*reg32++ = reg;
		} else if (reg16) {
			*reg16++ = (u16)reg;
		} else {
			*reg24++ = (u8)reg;
			*reg24++ = (u8)(reg >> 8);
			*reg24++ = (u8)(reg >> 16);
		}
	}
	t_size += size;

	/* In case there is data left in FIFO */
	if (size)
		goto retry;
end:
	/* Update payload length */
	if (reg32)
		*length += t_size * pair->channels * 4;
	else if (reg16)
		*length += t_size * pair->channels * 2;
	else
		*length += t_size * pair->channels * 3;
}

/* config dma channel */
static int asrc_dmaconfig(struct fsl_asrc_pair *pair,
			  struct dma_chan *chan,
			  u32 dma_addr, dma_addr_t buf_addr, u32 buf_len,
			  int dir, int width)
{
	struct fsl_asrc *asrc = pair->asrc;
	struct device *dev = &asrc->pdev->dev;
	struct dma_slave_config slave_config;
	enum dma_slave_buswidth buswidth;
	unsigned int sg_len, max_period_size;
	struct scatterlist *sg;
	int ret, i;

	switch (width) {
	case 8:
		buswidth = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	case 16:
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 24:
		buswidth = DMA_SLAVE_BUSWIDTH_3_BYTES;
		break;
	case 32:
		buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		dev_err(dev, "invalid word width\n");
		return -EINVAL;
	}

	memset(&slave_config, 0, sizeof(slave_config));
	if (dir == IN) {
		slave_config.direction = DMA_MEM_TO_DEV;
		slave_config.dst_addr = dma_addr;
		slave_config.dst_addr_width = buswidth;
		slave_config.dst_maxburst = asrc->m2m_get_maxburst(IN, pair);
	} else {
		slave_config.direction = DMA_DEV_TO_MEM;
		slave_config.src_addr = dma_addr;
		slave_config.src_addr_width = buswidth;
		slave_config.src_maxburst = asrc->m2m_get_maxburst(OUT, pair);
	}

	ret = dmaengine_slave_config(chan, &slave_config);
	if (ret) {
		dev_err(dev, "failed to config dmaengine for %s task: %d\n",
			DIR_STR(dir), ret);
		return -EINVAL;
	}

	max_period_size = rounddown(ASRC_M2M_PERIOD_SIZE, width * pair->channels / 8);
	/* scatter gather mode */
	sg_len = buf_len / max_period_size;
	if (buf_len % max_period_size)
		sg_len += 1;

	sg = kmalloc_array(sg_len, sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return -ENOMEM;

	sg_init_table(sg, sg_len);
	for (i = 0; i < (sg_len - 1); i++) {
		sg_dma_address(&sg[i]) = buf_addr + i * max_period_size;
		sg_dma_len(&sg[i]) = max_period_size;
	}
	sg_dma_address(&sg[i]) = buf_addr + i * max_period_size;
	sg_dma_len(&sg[i]) = buf_len - i * max_period_size;

	pair->desc[dir] = dmaengine_prep_slave_sg(chan, sg, sg_len,
						  slave_config.direction,
						  DMA_PREP_INTERRUPT);
	kfree(sg);
	if (!pair->desc[dir]) {
		dev_err(dev, "failed to prepare dmaengine for %s task\n", DIR_STR(dir));
		return -EINVAL;
	}

	pair->desc[dir]->callback = ASRC_xPUT_DMA_CALLBACK(dir);
	pair->desc[dir]->callback_param = pair;

	return 0;
}

/* main function of converter */
static int asrc_m2m_device_run(struct fsl_asrc_pair *pair, struct snd_compr_task_runtime *task)
{
	struct fsl_asrc *asrc = pair->asrc;
	struct device *dev = &asrc->pdev->dev;
	enum asrc_pair_index index = pair->index;
	struct snd_dma_buffer *src_buf, *dst_buf;
	unsigned int in_buf_len;
	unsigned int out_dma_len;
	unsigned int width;
	u32 fifo_addr;
	int ret = 0;

	/* set ratio mod */
	if (asrc->m2m_set_ratio_mod) {
		if (pair->ratio_mod_flag) {
			asrc->m2m_set_ratio_mod(pair, pair->ratio_mod);
			pair->ratio_mod_flag = false;
		}
	}

	src_buf = &pair->dma_buffer[IN];
	dst_buf = &pair->dma_buffer[OUT];

	width = snd_pcm_format_physical_width(pair->sample_format[IN]);
	fifo_addr = asrc->paddr + asrc->get_fifo_addr(IN, index);

	in_buf_len = task->input_size;

	if (in_buf_len < width * pair->channels / 8 ||
	    in_buf_len > ASRC_M2M_BUFFER_SIZE ||
	    in_buf_len % (width * pair->channels / 8)) {
		dev_err(dev, "out buffer size is error: [%d]\n", in_buf_len);
		ret = -EINVAL;
		goto end;
	}

	/* dma config for output dma channel */
	ret = asrc_dmaconfig(pair,
			     pair->dma_chan[IN],
			     fifo_addr,
			     src_buf->addr,
			     in_buf_len, IN, width);
	if (ret) {
		dev_err(dev, "out dma config error\n");
		goto end;
	}

	width = snd_pcm_format_physical_width(pair->sample_format[OUT]);
	fifo_addr = asrc->paddr + asrc->get_fifo_addr(OUT, index);
	out_dma_len = asrc->m2m_calc_out_len(pair, in_buf_len);
	if (out_dma_len > 0 && out_dma_len <= ASRC_M2M_BUFFER_SIZE) {
		/* dma config for capture dma channel */
		ret = asrc_dmaconfig(pair,
				     pair->dma_chan[OUT],
				     fifo_addr,
				     dst_buf->addr,
				     out_dma_len, OUT, width);
		if (ret) {
			dev_err(dev, "cap dma config error\n");
			goto end;
		}
	} else if (out_dma_len > ASRC_M2M_BUFFER_SIZE) {
		dev_err(dev, "cap buffer size error\n");
		ret = -EINVAL;
		goto end;
	}

	reinit_completion(&pair->complete[IN]);
	reinit_completion(&pair->complete[OUT]);

	/* Submit DMA request */
	dmaengine_submit(pair->desc[IN]);
	dma_async_issue_pending(pair->desc[IN]->chan);
	if (out_dma_len > 0) {
		dmaengine_submit(pair->desc[OUT]);
		dma_async_issue_pending(pair->desc[OUT]->chan);
	}

	asrc->m2m_start(pair);

	if (!wait_for_completion_interruptible_timeout(&pair->complete[IN], 10 * HZ)) {
		dev_err(dev, "out DMA task timeout\n");
		ret = -ETIMEDOUT;
		goto end;
	}

	if (out_dma_len > 0) {
		if (!wait_for_completion_interruptible_timeout(&pair->complete[OUT], 10 * HZ)) {
			dev_err(dev, "cap DMA task timeout\n");
			ret = -ETIMEDOUT;
			goto end;
		}
	}

	/* read the last words from FIFO */
	asrc_read_last_fifo(pair, dst_buf->area, &out_dma_len);
	/* update payload length for capture */
	task->output_size = out_dma_len;
end:
	return ret;
}

static int fsl_asrc_m2m_comp_open(struct snd_compr_stream *stream)
{
	struct fsl_asrc *asrc = stream->private_data;
	struct snd_compr_runtime *runtime = stream->runtime;
	struct device *dev = &asrc->pdev->dev;
	struct fsl_asrc_pair *pair;
	int size, ret;

	pair = kzalloc(sizeof(*pair) + asrc->pair_priv_size, GFP_KERNEL);
	if (!pair)
		return -ENOMEM;

	pair->private = (void *)pair + sizeof(struct fsl_asrc_pair);
	pair->asrc = asrc;

	init_completion(&pair->complete[IN]);
	init_completion(&pair->complete[OUT]);

	runtime->private_data = pair;

	size = ASRC_M2M_BUFFER_SIZE;
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev, size, &pair->dma_buffer[IN]);
	if (ret)
		goto error_alloc_in_buf;

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev, size, &pair->dma_buffer[OUT]);
	if (ret)
		goto error_alloc_out_buf;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to power up asrc\n");
		goto err_pm_runtime;
	}

	return 0;

err_pm_runtime:
	snd_dma_free_pages(&pair->dma_buffer[OUT]);
error_alloc_out_buf:
	snd_dma_free_pages(&pair->dma_buffer[IN]);
error_alloc_in_buf:
	kfree(pair);
	return ret;
}

static int fsl_asrc_m2m_comp_release(struct snd_compr_stream *stream)
{
	struct fsl_asrc *asrc = stream->private_data;
	struct snd_compr_runtime *runtime = stream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	struct device *dev = &asrc->pdev->dev;

	pm_runtime_put_sync(dev);

	snd_dma_free_pages(&pair->dma_buffer[IN]);
	snd_dma_free_pages(&pair->dma_buffer[OUT]);

	kfree(runtime->private_data);

	return 0;
}

static int fsl_asrc_m2m_comp_set_params(struct snd_compr_stream *stream,
					struct snd_compr_params *params)
{
	struct fsl_asrc *asrc = stream->private_data;
	struct snd_compr_runtime *runtime = stream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	struct fsl_asrc_m2m_cap cap;
	int ret, i;

	ret = asrc->m2m_get_cap(&cap);
	if (ret)
		return -EINVAL;

	if (pcm_format_to_bits((__force snd_pcm_format_t)params->codec.format) & cap.fmt_in)
		pair->sample_format[IN] = (__force snd_pcm_format_t)params->codec.format;
	else
		return -EINVAL;

	if (pcm_format_to_bits((__force snd_pcm_format_t)params->codec.pcm_format) & cap.fmt_out)
		pair->sample_format[OUT] = (__force snd_pcm_format_t)params->codec.pcm_format;
	else
		return -EINVAL;

	/* check input rate is in scope */
	for (i = 0; i < cap.rate_in_count; i++)
		if (params->codec.sample_rate == cap.rate_in[i]) {
			pair->rate[IN] = params->codec.sample_rate;
			break;
		}
	if (i == cap.rate_in_count)
		return -EINVAL;

	/* check output rate is in scope */
	for (i = 0; i < cap.rate_out_count; i++)
		if (params->codec.options.src_d.out_sample_rate == cap.rate_out[i]) {
			pair->rate[OUT] = params->codec.options.src_d.out_sample_rate;
			break;
		}
	if (i == cap.rate_out_count)
		return -EINVAL;

	if (params->codec.ch_in != params->codec.ch_out ||
	    params->codec.ch_in < cap.chan_min ||
	    params->codec.ch_in > cap.chan_max)
		return -EINVAL;

	pair->channels = params->codec.ch_in;
	pair->buf_len[IN] = params->buffer.fragment_size;
	pair->buf_len[OUT] = params->buffer.fragment_size;

	return 0;
}

static int fsl_asrc_m2m_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct snd_dma_buffer *dmab = dmabuf->priv;

	return snd_dma_buffer_mmap(dmab, vma);
}

static struct sg_table *fsl_asrc_m2m_map_dma_buf(struct dma_buf_attachment *attachment,
						 enum dma_data_direction direction)
{
	struct snd_dma_buffer *dmab = attachment->dmabuf->priv;
	struct sg_table *sgt;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	if (dma_get_sgtable(attachment->dev, sgt, dmab->area, dmab->addr, dmab->bytes) < 0)
		goto free;

	if (dma_map_sgtable(attachment->dev, sgt, direction, 0))
		goto free;

	return sgt;

free:
	sg_free_table(sgt);
	kfree(sgt);
	return NULL;
}

static void fsl_asrc_m2m_unmap_dma_buf(struct dma_buf_attachment *attachment,
				       struct sg_table *table,
				       enum dma_data_direction direction)
{
	dma_unmap_sgtable(attachment->dev, table, direction, 0);
}

static void fsl_asrc_m2m_release(struct dma_buf *dmabuf)
{
	/* buffer is released by fsl_asrc_m2m_comp_release() */
}

static const struct dma_buf_ops fsl_asrc_m2m_dma_buf_ops = {
	.mmap = fsl_asrc_m2m_mmap,
	.map_dma_buf = fsl_asrc_m2m_map_dma_buf,
	.unmap_dma_buf = fsl_asrc_m2m_unmap_dma_buf,
	.release = fsl_asrc_m2m_release,
};

static int fsl_asrc_m2m_comp_task_create(struct snd_compr_stream *stream,
					 struct snd_compr_task_runtime *task)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info_in);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info_out);
	struct fsl_asrc *asrc = stream->private_data;
	struct snd_compr_runtime *runtime = stream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	struct device *dev = &asrc->pdev->dev;
	int ret;

	exp_info_in.ops = &fsl_asrc_m2m_dma_buf_ops;
	exp_info_in.size = ASRC_M2M_BUFFER_SIZE;
	exp_info_in.flags = O_RDWR;
	exp_info_in.priv = &pair->dma_buffer[IN];
	task->input = dma_buf_export(&exp_info_in);
	if (IS_ERR(task->input)) {
		ret = PTR_ERR(task->input);
		return ret;
	}

	exp_info_out.ops = &fsl_asrc_m2m_dma_buf_ops;
	exp_info_out.size = ASRC_M2M_BUFFER_SIZE;
	exp_info_out.flags = O_RDWR;
	exp_info_out.priv = &pair->dma_buffer[OUT];
	task->output = dma_buf_export(&exp_info_out);
	if (IS_ERR(task->output)) {
		ret = PTR_ERR(task->output);
		return ret;
	}

	/* Request asrc pair/context */
	ret = asrc->request_pair(pair->channels, pair);
	if (ret) {
		dev_err(dev, "failed to request pair: %d\n", ret);
		goto err_request_pair;
	}

	ret = asrc->m2m_prepare(pair);
	if (ret) {
		dev_err(dev, "failed to start pair part one: %d\n", ret);
		goto err_start_part_one;
	}

	/* Request dma channels */
	pair->dma_chan[IN] = asrc->get_dma_channel(pair, IN);
	if (!pair->dma_chan[IN]) {
		dev_err(dev, "[ctx%d] failed to get input DMA channel\n", pair->index);
		ret = -EBUSY;
		goto err_dma_channel_in;
	}

	pair->dma_chan[OUT] = asrc->get_dma_channel(pair, OUT);
	if (!pair->dma_chan[OUT]) {
		dev_err(dev, "[ctx%d] failed to get output DMA channel\n", pair->index);
		ret = -EBUSY;
		goto err_dma_channel_out;
	}

	return 0;

err_dma_channel_out:
	dma_release_channel(pair->dma_chan[IN]);
err_dma_channel_in:
	if (asrc->m2m_unprepare)
		asrc->m2m_unprepare(pair);
err_start_part_one:
	asrc->release_pair(pair);
err_request_pair:
	return ret;
}

static int fsl_asrc_m2m_comp_task_start(struct snd_compr_stream *stream,
					struct snd_compr_task_runtime *task)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;

	return asrc_m2m_device_run(pair, task);
}

static int fsl_asrc_m2m_comp_task_stop(struct snd_compr_stream *stream,
				       struct snd_compr_task_runtime *task)
{
	return 0;
}

static int fsl_asrc_m2m_comp_task_free(struct snd_compr_stream *stream,
				       struct snd_compr_task_runtime *task)
{
	struct fsl_asrc *asrc = stream->private_data;
	struct snd_compr_runtime *runtime = stream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;

	/* Stop & release pair/context */
	if (asrc->m2m_stop)
		asrc->m2m_stop(pair);

	if (asrc->m2m_unprepare)
		asrc->m2m_unprepare(pair);
	asrc->release_pair(pair);

	/* Release dma channel */
	if (pair->dma_chan[IN])
		dma_release_channel(pair->dma_chan[IN]);
	if (pair->dma_chan[OUT])
		dma_release_channel(pair->dma_chan[OUT]);

	return 0;
}

static int fsl_asrc_m2m_get_caps(struct snd_compr_stream *cstream,
				 struct snd_compr_caps *caps)
{
	caps->num_codecs = 1;
	caps->min_fragment_size = 4096;
	caps->max_fragment_size = 4096;
	caps->min_fragments = 1;
	caps->max_fragments = 1;
	caps->codecs[0] = SND_AUDIOCODEC_PCM;

	return 0;
}

static int fsl_asrc_m2m_fill_codec_caps(struct fsl_asrc *asrc,
					struct snd_compr_codec_caps *codec)
{
	struct fsl_asrc_m2m_cap cap;
	snd_pcm_format_t k;
	int j = 0;
	int ret;

	ret = asrc->m2m_get_cap(&cap);
	if (ret)
		return -EINVAL;

	pcm_for_each_format(k) {
		if (pcm_format_to_bits(k) & cap.fmt_in) {
			codec->descriptor[j].max_ch = cap.chan_max;
			memcpy(codec->descriptor[j].sample_rates,
			       cap.rate_in,
			       cap.rate_in_count * sizeof(__u32));
			codec->descriptor[j].num_sample_rates = cap.rate_in_count;
			codec->descriptor[j].formats = (__force __u32)k;
			codec->descriptor[j].pcm_formats = cap.fmt_out;
			codec->descriptor[j].src.out_sample_rate_min = cap.rate_out[0];
			codec->descriptor[j].src.out_sample_rate_max =
				cap.rate_out[cap.rate_out_count - 1];
			j++;
		}
	}

	codec->codec = SND_AUDIOCODEC_PCM;
	codec->num_descriptors = j;
	return 0;
}

static int fsl_asrc_m2m_get_codec_caps(struct snd_compr_stream *stream,
				       struct snd_compr_codec_caps *codec)
{
	struct fsl_asrc *asrc = stream->private_data;

	return fsl_asrc_m2m_fill_codec_caps(asrc, codec);
}

static struct snd_compr_ops fsl_asrc_m2m_compr_ops = {
	.open = fsl_asrc_m2m_comp_open,
	.free = fsl_asrc_m2m_comp_release,
	.set_params = fsl_asrc_m2m_comp_set_params,
	.get_caps = fsl_asrc_m2m_get_caps,
	.get_codec_caps = fsl_asrc_m2m_get_codec_caps,
	.task_create = fsl_asrc_m2m_comp_task_create,
	.task_start = fsl_asrc_m2m_comp_task_start,
	.task_stop = fsl_asrc_m2m_comp_task_stop,
	.task_free = fsl_asrc_m2m_comp_task_free,
};

int fsl_asrc_m2m_suspend(struct fsl_asrc *asrc)
{
	struct fsl_asrc_pair *pair;
	int i;

	for (i = 0; i < PAIR_CTX_NUM; i++) {
		pair = asrc->pair[i];
		if (!pair || !pair->dma_buffer[IN].area || !pair->dma_buffer[OUT].area)
			continue;
		if (!completion_done(&pair->complete[IN])) {
			if (pair->dma_chan[IN])
				dmaengine_terminate_all(pair->dma_chan[IN]);
			asrc_input_dma_callback((void *)pair);
		}
		if (!completion_done(&pair->complete[OUT])) {
			if (pair->dma_chan[OUT])
				dmaengine_terminate_all(pair->dma_chan[OUT]);
			asrc_output_dma_callback((void *)pair);
		}

		if (asrc->m2m_pair_suspend)
			asrc->m2m_pair_suspend(pair);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fsl_asrc_m2m_suspend);

int fsl_asrc_m2m_resume(struct fsl_asrc *asrc)
{
	struct fsl_asrc_pair *pair;
	int i;

	for (i = 0; i < PAIR_CTX_NUM; i++) {
		pair = asrc->pair[i];
		if (!pair)
			continue;
		if (asrc->m2m_pair_resume)
			asrc->m2m_pair_resume(pair);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fsl_asrc_m2m_resume);

int fsl_asrc_m2m_init(struct fsl_asrc *asrc)
{
	struct device *dev = &asrc->pdev->dev;
	struct snd_card *card;
	struct snd_compr *compr;
	int ret;

	ret = snd_card_new(dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, 0, &card);
	if (ret < 0)
		return ret;

	strscpy(card->driver, "fsl-asrc-m2m", sizeof(card->driver));
	strscpy(card->shortname, "ASRC-M2M", sizeof(card->shortname));
	strscpy(card->longname, "ASRC-M2M", sizeof(card->shortname));

	asrc->card = card;

	compr = devm_kzalloc(dev, sizeof(*compr), GFP_KERNEL);
	if (!compr) {
		ret = -ENOMEM;
		goto err;
	}

	compr->ops = &fsl_asrc_m2m_compr_ops;
	compr->private_data = asrc;

	ret = snd_compress_new(card, 0, SND_COMPRESS_ACCEL, "ASRC M2M", compr);
	if (ret < 0)
		goto err;

	ret = snd_card_register(card);
	if (ret < 0)
		goto err;

	return 0;
err:
	snd_card_free(card);
	return ret;
}
EXPORT_SYMBOL_GPL(fsl_asrc_m2m_init);

void fsl_asrc_m2m_exit(struct fsl_asrc *asrc)
{
	struct snd_card *card = asrc->card;

	snd_card_free(card);
}
EXPORT_SYMBOL_GPL(fsl_asrc_m2m_exit);

MODULE_IMPORT_NS("DMA_BUF");
MODULE_AUTHOR("Shengjiu Wang <Shengjiu.Wang@nxp.com>");
MODULE_DESCRIPTION("Freescale ASRC M2M driver");
MODULE_LICENSE("GPL");
