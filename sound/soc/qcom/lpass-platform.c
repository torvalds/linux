// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * lpass-platform.c -- ALSA SoC platform driver for QTi LPASS
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "lpass-lpaif-reg.h"
#include "lpass.h"

#define DRV_NAME "lpass-platform"

struct lpass_pcm_data {
	int dma_ch;
	int i2s_port;
};

#define LPASS_PLATFORM_BUFFER_SIZE	(16 * 1024)
#define LPASS_PLATFORM_PERIODS		2

static const struct snd_pcm_hardware lpass_platform_pcm_hardware = {
	.info			=	SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_RESUME,
	.formats		=	SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
	.rates			=	SNDRV_PCM_RATE_8000_192000,
	.rate_min		=	8000,
	.rate_max		=	192000,
	.channels_min		=	1,
	.channels_max		=	8,
	.buffer_bytes_max	=	LPASS_PLATFORM_BUFFER_SIZE,
	.period_bytes_max	=	LPASS_PLATFORM_BUFFER_SIZE /
						LPASS_PLATFORM_PERIODS,
	.period_bytes_min	=	LPASS_PLATFORM_BUFFER_SIZE /
						LPASS_PLATFORM_PERIODS,
	.periods_min		=	LPASS_PLATFORM_PERIODS,
	.periods_max		=	LPASS_PLATFORM_PERIODS,
	.fifo_size		=	0,
};

static int lpass_platform_pcmops_open(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct lpass_variant *v = drvdata->variant;
	int ret, dma_ch, dir = substream->stream;
	struct lpass_pcm_data *data;

	data = devm_kzalloc(soc_runtime->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->i2s_port = cpu_dai->driver->id;
	runtime->private_data = data;

	if (v->alloc_dma_channel)
		dma_ch = v->alloc_dma_channel(drvdata, dir);
	else
		dma_ch = 0;

	if (dma_ch < 0)
		return dma_ch;

	drvdata->substream[dma_ch] = substream;

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_DMACTL_REG(v, dma_ch, dir), 0);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error writing to rdmactl reg: %d\n", ret);
		return ret;
	}

	data->dma_ch = dma_ch;

	snd_soc_set_runtime_hwparams(substream, &lpass_platform_pcm_hardware);

	runtime->dma_bytes = lpass_platform_pcm_hardware.buffer_bytes_max;

	ret = snd_pcm_hw_constraint_integer(runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(soc_runtime->dev, "setting constraints failed: %d\n",
			ret);
		return -EINVAL;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int lpass_platform_pcmops_close(struct snd_soc_component *component,
				       struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct lpass_variant *v = drvdata->variant;
	struct lpass_pcm_data *data;

	data = runtime->private_data;
	drvdata->substream[data->dma_ch] = NULL;
	if (v->free_dma_channel)
		v->free_dma_channel(drvdata, data->dma_ch);

	return 0;
}

static int lpass_platform_pcmops_hw_params(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	struct lpass_variant *v = drvdata->variant;
	snd_pcm_format_t format = params_format(params);
	unsigned int channels = params_channels(params);
	unsigned int regval;
	int ch, dir = substream->stream;
	int bitwidth;
	int ret, dma_port = pcm_data->i2s_port + v->dmactl_audif_start;

	ch = pcm_data->dma_ch;

	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(soc_runtime->dev, "invalid bit width given: %d\n",
				bitwidth);
		return bitwidth;
	}

	regval = LPAIF_DMACTL_BURSTEN_INCR4 |
			LPAIF_DMACTL_AUDINTF(dma_port) |
			LPAIF_DMACTL_FIFOWM_8;

	switch (bitwidth) {
	case 16:
		switch (channels) {
		case 1:
		case 2:
			regval |= LPAIF_DMACTL_WPSCNT_ONE;
			break;
		case 4:
			regval |= LPAIF_DMACTL_WPSCNT_TWO;
			break;
		case 6:
			regval |= LPAIF_DMACTL_WPSCNT_THREE;
			break;
		case 8:
			regval |= LPAIF_DMACTL_WPSCNT_FOUR;
			break;
		default:
			dev_err(soc_runtime->dev,
				"invalid PCM config given: bw=%d, ch=%u\n",
				bitwidth, channels);
			return -EINVAL;
		}
		break;
	case 24:
	case 32:
		switch (channels) {
		case 1:
			regval |= LPAIF_DMACTL_WPSCNT_ONE;
			break;
		case 2:
			regval |= LPAIF_DMACTL_WPSCNT_TWO;
			break;
		case 4:
			regval |= LPAIF_DMACTL_WPSCNT_FOUR;
			break;
		case 6:
			regval |= LPAIF_DMACTL_WPSCNT_SIX;
			break;
		case 8:
			regval |= LPAIF_DMACTL_WPSCNT_EIGHT;
			break;
		default:
			dev_err(soc_runtime->dev,
				"invalid PCM config given: bw=%d, ch=%u\n",
				bitwidth, channels);
			return -EINVAL;
		}
		break;
	default:
		dev_err(soc_runtime->dev, "invalid PCM config given: bw=%d, ch=%u\n",
			bitwidth, channels);
		return -EINVAL;
	}

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_DMACTL_REG(v, ch, dir), regval);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int lpass_platform_pcmops_hw_free(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	struct lpass_variant *v = drvdata->variant;
	unsigned int reg;
	int ret;

	reg = LPAIF_DMACTL_REG(v, pcm_data->dma_ch, substream->stream);
	ret = regmap_write(drvdata->lpaif_map, reg, 0);
	if (ret)
		dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n",
			ret);

	return ret;
}

static int lpass_platform_pcmops_prepare(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	struct lpass_variant *v = drvdata->variant;
	int ret, ch, dir = substream->stream;

	ch = pcm_data->dma_ch;

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_DMABASE_REG(v, ch, dir),
			runtime->dma_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmabase reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_DMABUFF_REG(v, ch, dir),
			(snd_pcm_lib_buffer_bytes(substream) >> 2) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmabuff reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_DMAPER_REG(v, ch, dir),
			(snd_pcm_lib_period_bytes(substream) >> 2) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmaper reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_update_bits(drvdata->lpaif_map,
			LPAIF_DMACTL_REG(v, ch, dir),
			LPAIF_DMACTL_ENABLE_MASK, LPAIF_DMACTL_ENABLE_ON);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int lpass_platform_pcmops_trigger(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream,
					 int cmd)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	struct lpass_variant *v = drvdata->variant;
	int ret, ch, dir = substream->stream;

	ch = pcm_data->dma_ch;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* clear status before enabling interrupts */
		ret = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ALL(ch));
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to irqclear reg: %d\n", ret);
			return ret;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ALL(ch),
				LPAIF_IRQ_ALL(ch));
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to irqen reg: %d\n", ret);
			return ret;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_DMACTL_REG(v, ch, dir),
				LPAIF_DMACTL_ENABLE_MASK,
				LPAIF_DMACTL_ENABLE_ON);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to rdmactl reg: %d\n", ret);
			return ret;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_DMACTL_REG(v, ch, dir),
				LPAIF_DMACTL_ENABLE_MASK,
				LPAIF_DMACTL_ENABLE_OFF);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to rdmactl reg: %d\n", ret);
			return ret;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ALL(ch), 0);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to irqen reg: %d\n", ret);
			return ret;
		}
		break;
	}

	return 0;
}

static snd_pcm_uframes_t lpass_platform_pcmops_pointer(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	struct lpass_variant *v = drvdata->variant;
	unsigned int base_addr, curr_addr;
	int ret, ch, dir = substream->stream;

	ch = pcm_data->dma_ch;

	ret = regmap_read(drvdata->lpaif_map,
			LPAIF_DMABASE_REG(v, ch, dir), &base_addr);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error reading from rdmabase reg: %d\n", ret);
		return ret;
	}

	ret = regmap_read(drvdata->lpaif_map,
			LPAIF_DMACURR_REG(v, ch, dir), &curr_addr);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error reading from rdmacurr reg: %d\n", ret);
		return ret;
	}

	return bytes_to_frames(substream->runtime, curr_addr - base_addr);
}

static int lpass_platform_pcmops_mmap(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream,
				      struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_coherent(substream->pcm->card->dev, vma,
			runtime->dma_area, runtime->dma_addr,
			runtime->dma_bytes);
}

static irqreturn_t lpass_dma_interrupt_handler(
			struct snd_pcm_substream *substream,
			struct lpass_data *drvdata,
			int chan, u32 interrupts)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_variant *v = drvdata->variant;
	irqreturn_t ret = IRQ_NONE;
	int rv;

	if (interrupts & LPAIF_IRQ_PER(chan)) {
		rv = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_PER(chan));
		if (rv) {
			dev_err(soc_runtime->dev,
				"error writing to irqclear reg: %d\n", rv);
			return IRQ_NONE;
		}
		snd_pcm_period_elapsed(substream);
		ret = IRQ_HANDLED;
	}

	if (interrupts & LPAIF_IRQ_XRUN(chan)) {
		rv = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_XRUN(chan));
		if (rv) {
			dev_err(soc_runtime->dev,
				"error writing to irqclear reg: %d\n", rv);
			return IRQ_NONE;
		}
		dev_warn(soc_runtime->dev, "xrun warning\n");
		snd_pcm_stop_xrun(substream);
		ret = IRQ_HANDLED;
	}

	if (interrupts & LPAIF_IRQ_ERR(chan)) {
		rv = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ERR(chan));
		if (rv) {
			dev_err(soc_runtime->dev,
				"error writing to irqclear reg: %d\n", rv);
			return IRQ_NONE;
		}
		dev_err(soc_runtime->dev, "bus access error\n");
		snd_pcm_stop(substream, SNDRV_PCM_STATE_DISCONNECTED);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t lpass_platform_lpaif_irq(int irq, void *data)
{
	struct lpass_data *drvdata = data;
	struct lpass_variant *v = drvdata->variant;
	unsigned int irqs;
	int rv, chan;

	rv = regmap_read(drvdata->lpaif_map,
			LPAIF_IRQSTAT_REG(v, LPAIF_IRQ_PORT_HOST), &irqs);
	if (rv) {
		pr_err("error reading from irqstat reg: %d\n", rv);
		return IRQ_NONE;
	}

	/* Handle per channel interrupts */
	for (chan = 0; chan < LPASS_MAX_DMA_CHANNELS; chan++) {
		if (irqs & LPAIF_IRQ_ALL(chan) && drvdata->substream[chan]) {
			rv = lpass_dma_interrupt_handler(
						drvdata->substream[chan],
						drvdata, chan, irqs);
			if (rv != IRQ_HANDLED)
				return rv;
		}
	}

	return IRQ_HANDLED;
}

static int lpass_platform_pcm_new(struct snd_soc_component *component,
				  struct snd_soc_pcm_runtime *soc_runtime)
{
	struct snd_pcm *pcm = soc_runtime->pcm;
	struct snd_pcm_substream *psubstream, *csubstream;
	int ret = -EINVAL;
	size_t size = lpass_platform_pcm_hardware.buffer_bytes_max;

	psubstream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (psubstream) {
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
					component->dev,
					size, &psubstream->dma_buffer);
		if (ret) {
			dev_err(soc_runtime->dev, "Cannot allocate buffer(s)\n");
			return ret;
		}
	}

	csubstream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	if (csubstream) {
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
					component->dev,
					size, &csubstream->dma_buffer);
		if (ret) {
			dev_err(soc_runtime->dev, "Cannot allocate buffer(s)\n");
			if (psubstream)
				snd_dma_free_pages(&psubstream->dma_buffer);
			return ret;
		}

	}

	return 0;
}

static void lpass_platform_pcm_free(struct snd_soc_component *component,
				    struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	int i;

	for_each_pcm_streams(i) {
		substream = pcm->streams[i].substream;
		if (substream) {
			snd_dma_free_pages(&substream->dma_buffer);
			substream->dma_buffer.area = NULL;
			substream->dma_buffer.addr = 0;
		}
	}
}

static const struct snd_soc_component_driver lpass_component_driver = {
	.name		= DRV_NAME,
	.open		= lpass_platform_pcmops_open,
	.close		= lpass_platform_pcmops_close,
	.hw_params	= lpass_platform_pcmops_hw_params,
	.hw_free	= lpass_platform_pcmops_hw_free,
	.prepare	= lpass_platform_pcmops_prepare,
	.trigger	= lpass_platform_pcmops_trigger,
	.pointer	= lpass_platform_pcmops_pointer,
	.mmap		= lpass_platform_pcmops_mmap,
	.pcm_construct	= lpass_platform_pcm_new,
	.pcm_destruct	= lpass_platform_pcm_free,

};

int asoc_qcom_lpass_platform_register(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);
	struct lpass_variant *v = drvdata->variant;
	int ret;

	drvdata->lpaif_irq = platform_get_irq_byname(pdev, "lpass-irq-lpaif");
	if (drvdata->lpaif_irq < 0)
		return -ENODEV;

	/* ensure audio hardware is disabled */
	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST), 0);
	if (ret) {
		dev_err(&pdev->dev, "error writing to irqen reg: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, drvdata->lpaif_irq,
			lpass_platform_lpaif_irq, IRQF_TRIGGER_RISING,
			"lpass-irq-lpaif", drvdata);
	if (ret) {
		dev_err(&pdev->dev, "irq request failed: %d\n", ret);
		return ret;
	}


	return devm_snd_soc_register_component(&pdev->dev,
			&lpass_component_driver, NULL, 0);
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_platform_register);

MODULE_DESCRIPTION("QTi LPASS Platform Driver");
MODULE_LICENSE("GPL v2");
