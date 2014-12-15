/*
 * Au1000/Au1500/Au1100 Audio DMA support.
 *
 * (c) 2011 Manuel Lauss <manuel.lauss@googlemail.com>
 *
 * copied almost verbatim from the old ALSA driver, written by
 *			Charles Eidsness <charles@cooper-street.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1000_dma.h>

#include "psc.h"

struct pcm_period {
	u32 start;
	u32 relative_end;	/* relative to start of buffer */
	struct pcm_period *next;
};

struct audio_stream {
	struct snd_pcm_substream *substream;
	int dma;
	struct pcm_period *buffer;
	unsigned int period_size;
	unsigned int periods;
};

struct alchemy_pcm_ctx {
	struct audio_stream stream[2];	/* playback & capture */
};

static void au1000_release_dma_link(struct audio_stream *stream)
{
	struct pcm_period *pointer;
	struct pcm_period *pointer_next;

	stream->period_size = 0;
	stream->periods = 0;
	pointer = stream->buffer;
	if (!pointer)
		return;
	do {
		pointer_next = pointer->next;
		kfree(pointer);
		pointer = pointer_next;
	} while (pointer != stream->buffer);
	stream->buffer = NULL;
}

static int au1000_setup_dma_link(struct audio_stream *stream,
				 unsigned int period_bytes,
				 unsigned int periods)
{
	struct snd_pcm_substream *substream = stream->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pcm_period *pointer;
	unsigned long dma_start;
	int i;

	dma_start = virt_to_phys(runtime->dma_area);

	if (stream->period_size == period_bytes &&
	    stream->periods == periods)
		return 0; /* not changed */

	au1000_release_dma_link(stream);

	stream->period_size = period_bytes;
	stream->periods = periods;

	stream->buffer = kmalloc(sizeof(struct pcm_period), GFP_KERNEL);
	if (!stream->buffer)
		return -ENOMEM;
	pointer = stream->buffer;
	for (i = 0; i < periods; i++) {
		pointer->start = (u32)(dma_start + (i * period_bytes));
		pointer->relative_end = (u32) (((i+1) * period_bytes) - 0x1);
		if (i < periods - 1) {
			pointer->next = kmalloc(sizeof(struct pcm_period),
						GFP_KERNEL);
			if (!pointer->next) {
				au1000_release_dma_link(stream);
				return -ENOMEM;
			}
			pointer = pointer->next;
		}
	}
	pointer->next = stream->buffer;
	return 0;
}

static void au1000_dma_stop(struct audio_stream *stream)
{
	if (stream->buffer)
		disable_dma(stream->dma);
}

static void au1000_dma_start(struct audio_stream *stream)
{
	if (!stream->buffer)
		return;

	init_dma(stream->dma);
	if (get_dma_active_buffer(stream->dma) == 0) {
		clear_dma_done0(stream->dma);
		set_dma_addr0(stream->dma, stream->buffer->start);
		set_dma_count0(stream->dma, stream->period_size >> 1);
		set_dma_addr1(stream->dma, stream->buffer->next->start);
		set_dma_count1(stream->dma, stream->period_size >> 1);
	} else {
		clear_dma_done1(stream->dma);
		set_dma_addr1(stream->dma, stream->buffer->start);
		set_dma_count1(stream->dma, stream->period_size >> 1);
		set_dma_addr0(stream->dma, stream->buffer->next->start);
		set_dma_count0(stream->dma, stream->period_size >> 1);
	}
	enable_dma_buffers(stream->dma);
	start_dma(stream->dma);
}

static irqreturn_t au1000_dma_interrupt(int irq, void *ptr)
{
	struct audio_stream *stream = (struct audio_stream *)ptr;
	struct snd_pcm_substream *substream = stream->substream;

	switch (get_dma_buffer_done(stream->dma)) {
	case DMA_D0:
		stream->buffer = stream->buffer->next;
		clear_dma_done0(stream->dma);
		set_dma_addr0(stream->dma, stream->buffer->next->start);
		set_dma_count0(stream->dma, stream->period_size >> 1);
		enable_dma_buffer0(stream->dma);
		break;
	case DMA_D1:
		stream->buffer = stream->buffer->next;
		clear_dma_done1(stream->dma);
		set_dma_addr1(stream->dma, stream->buffer->next->start);
		set_dma_count1(stream->dma, stream->period_size >> 1);
		enable_dma_buffer1(stream->dma);
		break;
	case (DMA_D0 | DMA_D1):
		pr_debug("DMA %d missed interrupt.\n", stream->dma);
		au1000_dma_stop(stream);
		au1000_dma_start(stream);
		break;
	case (~DMA_D0 & ~DMA_D1):
		pr_debug("DMA %d empty irq.\n", stream->dma);
	}
	snd_pcm_period_elapsed(substream);
	return IRQ_HANDLED;
}

static const struct snd_pcm_hardware alchemy_pcm_hardware = {
	.info		  = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			    SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BATCH,
	.period_bytes_min = 1024,
	.period_bytes_max = 16 * 1024 - 1,
	.periods_min	  = 4,
	.periods_max	  = 255,
	.buffer_bytes_max = 128 * 1024,
	.fifo_size	  = 16,
};

static inline struct alchemy_pcm_ctx *ss_to_ctx(struct snd_pcm_substream *ss)
{
	struct snd_soc_pcm_runtime *rtd = ss->private_data;
	return snd_soc_platform_get_drvdata(rtd->platform);
}

static inline struct audio_stream *ss_to_as(struct snd_pcm_substream *ss)
{
	struct alchemy_pcm_ctx *ctx = ss_to_ctx(ss);
	return &(ctx->stream[ss->stream]);
}

static int alchemy_pcm_open(struct snd_pcm_substream *substream)
{
	struct alchemy_pcm_ctx *ctx = ss_to_ctx(substream);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int *dmaids, s = substream->stream;
	char *name;

	dmaids = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	if (!dmaids)
		return -ENODEV;	/* whoa, has ordering changed? */

	/* DMA setup */
	name = (s == SNDRV_PCM_STREAM_PLAYBACK) ? "audio-tx" : "audio-rx";
	ctx->stream[s].dma = request_au1000_dma(dmaids[s], name,
					au1000_dma_interrupt, 0,
					&ctx->stream[s]);
	set_dma_mode(ctx->stream[s].dma,
		     get_dma_mode(ctx->stream[s].dma) & ~DMA_NC);

	ctx->stream[s].substream = substream;
	ctx->stream[s].buffer = NULL;
	snd_soc_set_runtime_hwparams(substream, &alchemy_pcm_hardware);

	return 0;
}

static int alchemy_pcm_close(struct snd_pcm_substream *substream)
{
	struct alchemy_pcm_ctx *ctx = ss_to_ctx(substream);
	int stype = substream->stream;

	ctx->stream[stype].substream = NULL;
	free_au1000_dma(ctx->stream[stype].dma);

	return 0;
}

static int alchemy_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct audio_stream *stream = ss_to_as(substream);
	int err;

	err = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	err = au1000_setup_dma_link(stream,
				    params_period_bytes(hw_params),
				    params_periods(hw_params));
	if (err)
		snd_pcm_lib_free_pages(substream);

	return err;
}

static int alchemy_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct audio_stream *stream = ss_to_as(substream);
	au1000_release_dma_link(stream);
	return snd_pcm_lib_free_pages(substream);
}

static int alchemy_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct audio_stream *stream = ss_to_as(substream);
	int err = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		au1000_dma_start(stream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		au1000_dma_stop(stream);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static snd_pcm_uframes_t alchemy_pcm_pointer(struct snd_pcm_substream *ss)
{
	struct audio_stream *stream = ss_to_as(ss);
	long location;

	location = get_dma_residue(stream->dma);
	location = stream->buffer->relative_end - location;
	if (location == -1)
		location = 0;
	return bytes_to_frames(ss->runtime, location);
}

static struct snd_pcm_ops alchemy_pcm_ops = {
	.open			= alchemy_pcm_open,
	.close			= alchemy_pcm_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params	        = alchemy_pcm_hw_params,
	.hw_free	        = alchemy_pcm_hw_free,
	.trigger		= alchemy_pcm_trigger,
	.pointer		= alchemy_pcm_pointer,
};

static void alchemy_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int alchemy_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL), 65536, (4096 * 1024) - 1);

	return 0;
}

static struct snd_soc_platform_driver alchemy_pcm_soc_platform = {
	.ops		= &alchemy_pcm_ops,
	.pcm_new	= alchemy_pcm_new,
	.pcm_free	= alchemy_pcm_free_dma_buffers,
};

static int alchemy_pcm_drvprobe(struct platform_device *pdev)
{
	struct alchemy_pcm_ctx *ctx;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	platform_set_drvdata(pdev, ctx);

	return snd_soc_register_platform(&pdev->dev, &alchemy_pcm_soc_platform);
}

static int alchemy_pcm_drvremove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);

	return 0;
}

static struct platform_driver alchemy_pcmdma_driver = {
	.driver	= {
		.name	= "alchemy-pcm-dma",
	},
	.probe		= alchemy_pcm_drvprobe,
	.remove		= alchemy_pcm_drvremove,
};

module_platform_driver(alchemy_pcmdma_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Au1000/Au1500/Au1100 Audio DMA driver");
MODULE_AUTHOR("Manuel Lauss");
