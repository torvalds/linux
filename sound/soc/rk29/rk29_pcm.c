/*
 * rk29_pcm.c  --  ALSA SoC ROCKCHIP PCM Audio Layer Platform driver
 *
 * Driver for rockchip pcm audio
 *
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/dma.h>
#include <mach/hardware.h>
#include <mach/dma-pl330.h>

#include "rk29_pcm.h"

#define PCM_DMA_DEBUG 0

#if 0
#define DBG(x...) printk(KERN_DEBUG x)
#else
#define DBG(x...) do { } while (0)
#endif


static const struct snd_pcm_hardware rockchip_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				    SNDRV_PCM_INFO_BLOCK_TRANSFER |
				    SNDRV_PCM_INFO_MMAP |
				    SNDRV_PCM_INFO_MMAP_VALID |
				    SNDRV_PCM_INFO_PAUSE |
				    SNDRV_PCM_INFO_RESUME,
	.formats		=   SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min		= 2,
	.channels_max		= 8,
#ifdef CONFIG_RK_SRAM_DMA
	.buffer_bytes_max	= 24*1024,//period_bytes_max * periods_max
#else
	.buffer_bytes_max	= 128*1024,
#endif
	.period_bytes_min	= 64,  ///PAGE_SIZE,
#ifdef CONFIG_RK_SRAM_DMA
	.period_bytes_max	= 8*1024,
#else
	.period_bytes_max	= 2048*4,///PAGE_SIZE*2,
#endif
	.periods_min		= 3,///2,
	.periods_max		= 128,
	.fifo_size		= 16,
};


struct rockchip_dma_buf_set {
	struct rockchip_dma_buf_set	*next;
	struct scatterlist sg;
};

struct rockchip_runtime_data {
	spinlock_t lock;
	int state;
	int transfer_first;
	unsigned int dma_loaded;
	unsigned int dma_limit;
	unsigned int dma_period;
	dma_addr_t dma_start;
	dma_addr_t dma_pos;
	dma_addr_t dma_end;
	struct rockchip_pcm_dma_params *params;
	struct rockchip_dma_buf_set	*curr;		/* current dma buffer set */
	struct rockchip_dma_buf_set	*next;		/* next buffer set to load */
	struct rockchip_dma_buf_set	*end;		/* end of queue set*/
};


/* rockchip_pcm_enqueue
 *
 * place a dma buffer onto the queue for the dma system
 * to handle.
*/
static void rockchip_pcm_enqueue(struct snd_pcm_substream *substream)
{
	struct rockchip_runtime_data *prtd = substream->runtime->private_data;	
	dma_addr_t pos = prtd->dma_pos;
	int ret;

//	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        
	while (prtd->dma_loaded < prtd->dma_limit) {
		unsigned long len = prtd->dma_period;
//	DBG("dma_loaded: %d\n", prtd->dma_loaded);
		if ((pos + len) > prtd->dma_end) {
			len  = prtd->dma_end - pos;
		}

		if((len%(prtd->params->dma_size*16) == 0) && (prtd->params->flag == 1))                 
		{                                               
			ret = rk29_dma_config(prtd->params->channel,                                    
				prtd->params->dma_size, 16);                                            
			prtd->params->flag = 0;                                         
			DBG("size = 16, channel = %d, flag = %d\n",prtd->params->channel,prtd->params->flag);        
		}                               
		else if((len%(prtd->params->dma_size*16) != 0) && (prtd->params->flag == 0))            
		{                                               
			ret = rk29_dma_config(prtd->params->channel,                    
				prtd->params->dma_size, 1);                                             
			prtd->params->flag = 1;                                         
			DBG("size = 1, channel = %d, flag = %d\n",prtd->params->channel,prtd->params->flag);         
		}


		ret = rk29_dma_enqueue(prtd->params->channel,substream, pos, len);
//		if(prtd->params->channel == 2)
			DBG("Enter::%s, %d, ret=%d, Channel=%d, Addr=0x%X, Len=%lu\n",
                   __FUNCTION__,__LINE__, ret, prtd->params->channel, pos, len);		        
		if (ret == 0) {
			prtd->dma_loaded++;
			pos += prtd->dma_period;
			if (pos >= prtd->dma_end)
				pos = prtd->dma_start;
		} else 
			break;
	}

	prtd->dma_pos = pos;
}

void rk29_audio_buffdone(void *dev_id, int size,
				   enum rk29_dma_buffresult result)
{
	struct snd_pcm_substream *substream = dev_id;
	struct rockchip_runtime_data *prtd;
#if PCM_DMA_DEBUG
	static ktime_t before = {0},after = {0};
	s64 t;
	before = after;
	after = ktime_get();
	t = ktime_to_us(ktime_sub(after, before));
	if(result == RK29_RES_OK)
	{
		if(t > 23220+73 && t != ktime_to_us(after)) // 4096/4/44100 + 32/44100 
		{
			printk(KERN_DEBUG "Time out:: Audio DMA buffdone time out!!! the time = %lld!\n", t);
		}
		printk(KERN_DEBUG "audio DMA callback time = %lld\n", t);
	}
//	printk(KERN_DEBUG "a %d %d\n", size, result);
#endif
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	
	if (!substream){
		DBG("substream is free\n");
		return;
	}	
	if (!substream->runtime){
		DBG("substream->runtime is free\n");
		return;
	}	
	switch(result)
	{
	case RK29_RES_OK:
		break;
	case RK29_RES_ERR:
	case RK29_RES_ABORT:
		DBG("Enter::%s dma about or error result = %d \n",__FUNCTION__,result);
		return;
	}

	prtd = substream->runtime->private_data;
	
//	if(prtd->params->channel == 2)
		DBG("Enter::%s----%d   channel =%d \n",__FUNCTION__,__LINE__);	
	if(!(prtd->state & ST_RUNNING))
		return;	
	if (substream){
		snd_pcm_period_elapsed(substream);
	}
	spin_lock(&prtd->lock);
	prtd->dma_loaded--;
	if (prtd->state & ST_RUNNING) {
		rockchip_pcm_enqueue(substream);
	}
        spin_unlock(&prtd->lock);
   
}

static int rockchip_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct rockchip_pcm_dma_params *dma = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	struct rockchip_pcm_dma_params *dma = snd_soc_dai_get_dma_data(rtd->dai->cpu_dai, substream);
#else
	struct rockchip_pcm_dma_params *dma = rtd->dai->cpu_dai->dma_data;
#endif
	unsigned long totbytes = params_buffer_bytes(params);
	int ret = 0;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma)
		return 0;

	/* this may get called several times by oss emulation
	 * with different params -HW */
	if (prtd->params == NULL) {
		/* prepare DMA */
		prtd->params = dma;
#ifdef CONFIG_SND_I2S_DMA_EVENT_DYNAMIC
		DBG("params %p, client %p, channel %d\n", prtd->params,prtd->params->client, prtd->params->channel);
		ret = rk29_dma_request(prtd->params->channel, prtd->params->client, NULL);
		DBG("Enter::%s, %d, ret=%d, Channel=%d\n", __FUNCTION__, __LINE__, ret, prtd->params->channel);
		if (ret) {
			DBG(KERN_ERR "failed to get dma channel\n");
			return ret;
		}
#endif
	}

	ret = rk29_dma_set_buffdone_fn(prtd->params->channel, rk29_audio_buffdone);
	if(ret < 0){
		DBG(KERN_ERR "failed to rk29_dma_set_buffdone_fn\n");
		return ret;
	}
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	runtime->dma_bytes = totbytes;

	spin_lock_irq(&prtd->lock);
	prtd->dma_loaded = 0;
	prtd->dma_limit = params_periods(params);//runtime->hw.periods_min;
	prtd->dma_period = params_period_bytes(params);
	prtd->dma_start = runtime->dma_addr;
	prtd->dma_pos = prtd->dma_start;
	prtd->dma_end = prtd->dma_start + totbytes;
	prtd->transfer_first = 1;
	prtd->curr = NULL;
	prtd->next = NULL;
	prtd->end = NULL;
	spin_unlock_irq(&prtd->lock);
	return ret;
}

static int rockchip_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct rockchip_runtime_data *prtd = substream->runtime->private_data;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	/* TODO - do we need to ensure DMA flushed */
	snd_pcm_set_runtime_buffer(substream, NULL);

	if (prtd->params) {
#ifdef CONFIG_SND_I2S_DMA_EVENT_DYNAMIC		
		rk29_dma_free(prtd->params->channel, prtd->params->client);
		prtd->params = NULL;
#endif		
	}

	return 0;
}

static int rockchip_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct rockchip_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!prtd->params)
		return 0;

        if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
                ret = rk29_dma_devconfig(prtd->params->channel, 
                               RK29_DMASRC_MEM, 
                               prtd->params->dma_addr);
        }else{
                ret = rk29_dma_devconfig(prtd->params->channel, 
                               RK29_DMASRC_HW, 
                               prtd->params->dma_addr);
        }
        DBG("Enter::%s, %d, ret=%d, Channel=%d, Addr=0x%X\n", __FUNCTION__, __LINE__, ret, prtd->params->channel, prtd->params->dma_addr);
        ret = rk29_dma_config(prtd->params->channel, 
                prtd->params->dma_size, 1);
		prtd->params->flag = 1;  

        DBG("Enter:%s, %d, ret = %d, Channel=%d, Size=%d\n", 
                __FUNCTION__, __LINE__, ret, prtd->params->channel, 
                prtd->params->dma_size);
                
        ret= rk29_dma_ctrl(prtd->params->channel, RK29_DMAOP_FLUSH);
        DBG("Enter:%s, %d, ret = %d, Channel=%d\n", 
                __FUNCTION__, __LINE__, ret, prtd->params->channel);
        
	prtd->dma_loaded = 0;
	prtd->dma_pos = prtd->dma_start;

	/* enqueue dma buffers */
	rockchip_pcm_enqueue(substream);
	return ret;
}

static int rockchip_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct rockchip_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;
	/**************add by qiuen for volume*****/
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct snd_soc_dai *pCodec_dai = rtd->codec_dai;
#else
	struct snd_soc_dai *pCodec_dai = rtd->dai->codec_dai;
#endif
	int vol = 0;
	int streamType = 0;
	
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	
	if(cmd==SNDRV_PCM_TRIGGER_VOLUME){
		vol = substream->number % 100;
		streamType = (substream->number / 100) % 100;
		DBG("enter:vol=%d,streamType=%d\n",vol,streamType);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		if(pCodec_dai->driver->ops->set_volume)
			pCodec_dai->driver->ops->set_volume(streamType, vol);
#else
		if(pCodec_dai->ops->set_volume)
			pCodec_dai->ops->set_volume(streamType, vol);
#endif
	}
	/****************************************************/
	spin_lock(&prtd->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	        DBG(" START \n");
	    prtd->state |= ST_RUNNING;
	    rk29_dma_ctrl(prtd->params->channel, RK29_DMAOP_START);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	    DBG(" RESUME \n");
	    break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		DBG(" RESTART \n");
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	    DBG(" STOPS \n");
		prtd->state &= ~ST_RUNNING;
		rk29_dma_ctrl(prtd->params->channel, RK29_DMAOP_STOP);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock(&prtd->lock);
	return ret;
}


static snd_pcm_uframes_t
rockchip_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_runtime_data *prtd = runtime->private_data;
	unsigned long res;
	dma_addr_t src, dst;
	snd_pcm_uframes_t ret;
    

	spin_lock(&prtd->lock);

	rk29_dma_getposition(prtd->params->channel, &src, &dst);
	
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		res = dst - prtd->dma_start;
	else
		res = src - prtd->dma_start;

	spin_unlock(&prtd->lock);

	ret = bytes_to_frames(runtime, res);
	if (ret == runtime->buffer_size)
		ret = 0;
		
	if(prtd->params->channel == 2)		
		DBG("Enter:%s src = %x res = %x ret = %d\n",__FUNCTION__,src,res,ret);	

	return ret;	
}


static int rockchip_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_runtime_data *prtd;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	snd_soc_set_runtime_hwparams(substream, &rockchip_pcm_hardware);

	prtd = kzalloc(sizeof(struct rockchip_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;
	return 0;
}

static int rockchip_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_runtime_data *prtd = runtime->private_data;
        struct rockchip_dma_buf_set *sg_buf = NULL;
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	if (!prtd) {
		DBG("rockchip_pcm_close called with prtd == NULL\n");
		return 0;
	}

	if (prtd->params)
		rk29_dma_set_buffdone_fn(prtd->params->channel, NULL);
	sg_buf = prtd->curr;

	while (sg_buf != NULL) {
		prtd->curr = sg_buf->next;
		prtd->next = sg_buf->next;
		sg_buf->next  = NULL;
		kfree(sg_buf);
		sg_buf = NULL;
		sg_buf = prtd->curr;
	}
	kfree(prtd);

	return 0;
}

static int rockchip_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops rockchip_pcm_ops = {
	.open		= rockchip_pcm_open,
	.close		= rockchip_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= rockchip_pcm_hw_params,
	.hw_free	= rockchip_pcm_hw_free,
	.prepare	= rockchip_pcm_prepare,
	.trigger	= rockchip_pcm_trigger,
	.pointer	= rockchip_pcm_pointer,
	.mmap		= rockchip_pcm_mmap,
};

#ifdef CONFIG_ARCH_RK30
#define SRAM_DMA_PHYS_PLAYBACK	(dma_addr_t)(RK30_IMEM_PHYS + 16*1024)
#define SRAM_DMA_START_PLAYBACK	(RK30_IMEM_NONCACHED + 16*1024)
#define SRAM_DMA_PHYS_CAPTURE 	(dma_addr_t)(SRAM_DMA_PHYS_PLAYBACK + 24*1024)
#define SRAM_DMA_START_CAPTURE	(SRAM_DMA_START_PLAYBACK + 24*1024)
#endif

static int rockchip_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = rockchip_pcm_hardware.buffer_bytes_max;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
#ifdef CONFIG_RK_SRAM_DMA
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		buf->area = SRAM_DMA_START_PLAYBACK;
		buf->addr = SRAM_DMA_PHYS_PLAYBACK;
	} else{
		buf->area = SRAM_DMA_START_CAPTURE;
		buf->addr = SRAM_DMA_PHYS_CAPTURE;		
	}
#else
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);
#endif
	if (!buf->area)
		return -ENOMEM;
	buf->bytes = size;
	DBG("%s: size %d\n",__FUNCTION__, size);
	return 0;
}

static void rockchip_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_writecombine(pcm->card->dev, buf->bytes,
				      buf->area, buf->addr);
		buf->area = NULL;
	}
}

static u64 rockchip_pcm_dmamask = DMA_BIT_MASK(32);

static int rockchip_pcm_new(struct snd_card *card,
	struct snd_soc_dai *dai, struct snd_pcm *pcm)
{
	int ret = 0;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &rockchip_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	if (dai->driver->playback.channels_min) {
#else
	if (dai->playback.channels_min) {
#endif
		ret = rockchip_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	if (dai->driver->capture.channels_min) {
#else
	if (dai->capture.channels_min) {
#endif
		ret = rockchip_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static struct snd_soc_platform_driver rockchip_pcm_platform = {
	.ops		= &rockchip_pcm_ops,
	.pcm_new	= rockchip_pcm_new,
	.pcm_free	= rockchip_pcm_free_dma_buffers,
};

static int __devinit rockchip_pcm_platform_probe(struct platform_device *pdev)
{
        DBG("Enter::%s, %d\n", __FUNCTION__, __LINE__);
	return snd_soc_register_platform(&pdev->dev, &rockchip_pcm_platform);
}

static int __devexit rockchip_pcm_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver rockchip_pcm_driver = {
	.driver = {
		.name = "rockchip-audio",
		.owner = THIS_MODULE,
	},
	.probe = rockchip_pcm_platform_probe,
	.remove = __devexit_p(rockchip_pcm_platform_remove),
};

static int __init snd_rockchip_pcm_init(void)
{
        DBG("Enter::%s, %d\n", __FUNCTION__, __LINE__);
	return platform_driver_register(&rockchip_pcm_driver);
}
module_init(snd_rockchip_pcm_init);

static void __exit snd_rockchip_pcm_exit(void)
{
	platform_driver_unregister(&rockchip_pcm_driver);
}
module_exit(snd_rockchip_pcm_exit);
#else
struct snd_soc_platform rk29_soc_platform = {
	.name		= "rockchip-audio",
	.pcm_ops 	= &rockchip_pcm_ops,
	.pcm_new	= rockchip_pcm_new,
	.pcm_free	= rockchip_pcm_free_dma_buffers,
};
EXPORT_SYMBOL_GPL(rk29_soc_platform);

static int __init rockchip_soc_platform_init(void)
{
        DBG("Enter::%s, %d\n", __FUNCTION__, __LINE__);
	return snd_soc_register_platform(&rk29_soc_platform);
}
module_init(rockchip_soc_platform_init);

static void __exit rockchip_soc_platform_exit(void)
{
	snd_soc_unregister_platform(&rk29_soc_platform);
}
module_exit(rockchip_soc_platform_exit);
#endif

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP PCM ASoC Interface");
MODULE_LICENSE("GPL");

