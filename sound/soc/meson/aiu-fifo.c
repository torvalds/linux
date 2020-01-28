// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu-fifo.h"

#define AIU_MEM_START	0x00
#define AIU_MEM_RD	0x04
#define AIU_MEM_END	0x08
#define AIU_MEM_MASKS	0x0c
#define  AIU_MEM_MASK_CH_RD GENMASK(7, 0)
#define  AIU_MEM_MASK_CH_MEM GENMASK(15, 8)
#define AIU_MEM_CONTROL	0x10
#define  AIU_MEM_CONTROL_INIT BIT(0)
#define  AIU_MEM_CONTROL_FILL_EN BIT(1)
#define  AIU_MEM_CONTROL_EMPTY_EN BIT(2)

snd_pcm_uframes_t aiu_fifo_pointer(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *runtime = substream->runtime; 
	unsigned int addr;

	snd_soc_component_read(component, fifo->hw->mem_offset + AIU_MEM_RD,
			       &addr);

	return bytes_to_frames(runtime, addr - (unsigned int)runtime->dma_addr);
}
EXPORT_SYMBOL_GPL(aiu_fifo_pointer);

static void aiu_fifo_enable(struct snd_soc_component *component,
			    bool enable)
{
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);
	unsigned int en_mask = (AIU_MEM_CONTROL_FILL_EN |
				AIU_MEM_CONTROL_EMPTY_EN);

	snd_soc_component_update_bits(component,
				      fifo->hw->mem_offset + AIU_MEM_CONTROL,
				      en_mask, enable ? en_mask : 0);
}

int aiu_fifo_trigger(struct snd_pcm_substream *substream, int cmd,
		     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	        aiu_fifo_enable(component, true);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		aiu_fifo_enable(component, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(aiu_fifo_trigger);


int aiu_fifo_prepare(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);

        snd_soc_component_update_bits(component,
				      fifo->hw->mem_offset + AIU_MEM_CONTROL,
				      AIU_MEM_CONTROL_INIT,
				      AIU_MEM_CONTROL_INIT);
	snd_soc_component_update_bits(component,
				      fifo->hw->mem_offset + AIU_MEM_CONTROL,
				      AIU_MEM_CONTROL_INIT, 0);
	return 0;
}
EXPORT_SYMBOL_GPL(aiu_fifo_prepare);

int aiu_fifo_hw_params(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *params,
		       struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);
	dma_addr_t end;
	int ret;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	/* Setup the fifo boundaries */
	end = runtime->dma_addr + runtime->dma_bytes - fifo->hw->fifo_block;
	snd_soc_component_write(component, fifo->hw->mem_offset + AIU_MEM_START,
				runtime->dma_addr);
	snd_soc_component_write(component, fifo->hw->mem_offset + AIU_MEM_RD,
				runtime->dma_addr);
	snd_soc_component_write(component, fifo->hw->mem_offset + AIU_MEM_END,
				end);

	/* Setup the fifo to read all the memory - no skip */
	snd_soc_component_update_bits(component,
				      fifo->hw->mem_offset + AIU_MEM_MASKS,
				      AIU_MEM_MASK_CH_RD | AIU_MEM_MASK_CH_MEM,
				      FIELD_PREP(AIU_MEM_MASK_CH_RD, 0xff) |
				      FIELD_PREP(AIU_MEM_MASK_CH_MEM, 0xff));

	return 0;
}
EXPORT_SYMBOL_GPL(aiu_fifo_hw_params);

int aiu_fifo_hw_free(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	return snd_pcm_lib_free_pages(substream);
}
EXPORT_SYMBOL_GPL(aiu_fifo_hw_free);

static irqreturn_t aiu_fifo_isr(int irq, void *dev_id)
{
	struct snd_pcm_substream *playback = dev_id;

	snd_pcm_period_elapsed(playback);

	return IRQ_HANDLED;
}

int aiu_fifo_startup(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);
	int ret;

	snd_soc_set_runtime_hwparams(substream, fifo->hw->pcm);

	/*
	 * Make sure the buffer and period size are multiple of the fifo burst
	 * size
	 */
	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 fifo->hw->fifo_block);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					 fifo->hw->fifo_block);
	if (ret)
		return ret;

	ret = clk_prepare_enable(fifo->pclk);
	if (ret)
		return ret;

	ret = request_irq(fifo->irq, aiu_fifo_isr, 0, dev_name(dai->dev),
			  substream);
	if (ret)
		clk_disable_unprepare(fifo->pclk);

	return ret;
}
EXPORT_SYMBOL_GPL(aiu_fifo_startup);

void aiu_fifo_shutdown(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);

	free_irq(fifo->irq, substream);
	clk_disable_unprepare(fifo->pclk);
}
EXPORT_SYMBOL_GPL(aiu_fifo_shutdown);

int aiu_fifo_pcm_new(struct snd_soc_pcm_runtime *rtd,
		     struct snd_soc_dai *dai)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);
	size_t size = fifo->hw->pcm->buffer_bytes_max;


	snd_pcm_lib_preallocate_pages(
		rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
		SNDRV_DMA_TYPE_DEV, card->dev, size, size);

	return 0;
}
EXPORT_SYMBOL_GPL(aiu_fifo_pcm_new);

int aiu_fifo_component_probe(struct snd_soc_component *component)
{
	struct device *dev = component->dev;
	struct regmap *map;

	map = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "Could not get regmap\n");
		return PTR_ERR(map);
	}

	snd_soc_component_init_regmap(component, map);

	return 0;
}
EXPORT_SYMBOL_GPL(aiu_fifo_component_probe);

int aiu_fifo_probe(struct platform_device *pdev)
{
	const struct aiu_fifo_match_data *data;
	struct device *dev = &pdev->dev;
	struct aiu_fifo *fifo;

	data = of_device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	fifo = devm_kzalloc(dev, sizeof(*fifo), GFP_KERNEL);
	if (!fifo)
		return -ENOMEM;
	platform_set_drvdata(pdev, fifo);
	fifo->hw = data->hw;

	fifo->pclk = devm_clk_get(dev, NULL);
	if (IS_ERR(fifo->pclk)) {
		if (PTR_ERR(fifo->pclk) != -EPROBE_DEFER)
			dev_err(dev, "failed to get pclk: %ld\n",
				PTR_ERR(fifo->pclk));
		return PTR_ERR(fifo->pclk);
	}

	fifo->irq = platform_get_irq(pdev, 0);
	if (fifo->irq <= 0) {
		dev_err(dev, "Can't get irq\n");
		return fifo->irq;
	}

	return devm_snd_soc_register_component(dev, data->component_drv,
					       data->dai_drv, 1);
}
EXPORT_SYMBOL_GPL(aiu_fifo_probe);

MODULE_DESCRIPTION("Amlogic AIU FIFO driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
