/*
 * aml_platform.c  --  ALSA audio platform interface for the AML Meson SoC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "aml_platform_pcm2bt.h"
#include "aml_pcm.h"
#include "aml_platform.h"
#include <linux/of.h>

#ifndef ARRY_SIZE
#define ARRY_SIZE(A)    (sizeof(A) /sizeof(A[0]))
#endif

static LIST_HEAD(stream_list);
static DEFINE_SPINLOCK(platform_lock);
    
struct aml_platform_stream{
    struct list_head list;
    struct aml_audio_interface *interface;
    struct snd_pcm_substream *substream;
};

static struct aml_audio_interface *audio_interfaces[] = {
    &aml_i2s_interface,
    &aml_pcm_interface,
};

static inline struct aml_audio_interface *find_audio_interface(int id)
{
    struct aml_audio_interface *interface = NULL;
    int i = 0;

    for (i=0; i<ARRAY_SIZE(audio_interfaces); i++) {
        if (audio_interfaces[i]->id == id) {
            interface = audio_interfaces[i];
            break;
        }
    }

    return interface;
}

static inline struct aml_platform_stream *find_platform_stream(struct snd_pcm_substream *substream)
{
    struct aml_platform_stream *plat_stream = NULL;
    struct list_head *entry = NULL;

    list_for_each(entry, &stream_list) {
        plat_stream = list_entry(entry, struct aml_platform_stream, list);
        if (plat_stream->substream == substream) {
            return plat_stream;
        }
    }

    return NULL;
}

static void dump_platform_stream(void)
{
    struct aml_platform_stream *plat_stream = NULL;
    struct list_head *entry = NULL;
    int n = 0;

    list_for_each(entry, &stream_list) {
        plat_stream = list_entry(entry, struct aml_platform_stream, list);
        printk(KERN_INFO "substream#%d ptr: %p type: %s name: %s interface: %s\n",
                        n,
                        plat_stream->substream,
                        (plat_stream->substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture",
                        plat_stream->substream->name,
                        plat_stream->interface->name);
        n++;
    }
}

static int aml_platform_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    printk(KERN_DEBUG "enter %s (codec_dai: %s %d cpu_dai: %s %d)\n",
                    __FUNCTION__, codec_dai->name, codec_dai->id, cpu_dai->name, cpu_dai->id);

    aud_interface = find_audio_interface(cpu_dai->id);
    if (unlikely(NULL == aud_interface)) {
        printk(KERN_ERR "aml-platform: no such audio interface!");
        ret = -ENODEV;
        goto out;
    }

    BUG_ON(aud_interface->pcm_ops->open == NULL);

	plat_stream = kzalloc(sizeof(struct aml_platform_stream), GFP_KERNEL);
	if (unlikely(plat_stream == NULL)) {
        printk(KERN_ERR "aml-platform: out of memory!");
		ret = -ENOMEM;
		goto out;
	}

    ret = aud_interface->pcm_ops->open(substream);
    if (ret >= 0) {
        INIT_LIST_HEAD(&plat_stream->list);
        plat_stream->substream = substream;
        plat_stream->interface = aud_interface;

        spin_lock(&platform_lock);
        list_add_tail(&plat_stream->list, &stream_list);
        spin_unlock(&platform_lock);

        dump_platform_stream();
    } else {
        printk(KERN_ERR "aml-platform: open pcm substream failed ret: %d!", ret);
    }

out:
    return ret;
}

static int aml_platform_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    printk(KERN_DEBUG "enter %s (codec_dai: %s %d cpu_dai: %s %d)\n",
                    __FUNCTION__, codec_dai->name, codec_dai->id, cpu_dai->name, cpu_dai->id);

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        BUG_ON(aud_interface->pcm_ops->close == NULL);

        ret = aud_interface->pcm_ops->close(substream);
        if (ret >= 0) {
            spin_lock(&platform_lock);
            list_del(&plat_stream->list);
            spin_unlock(&platform_lock);
            kfree(plat_stream);
        } else {
            printk(KERN_ERR "aml-platform: close pcm substream failed ret: %d!", ret);
        }
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static int aml_platform_ioctl(struct snd_pcm_substream * substream,
                unsigned int cmd, void *arg)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        if (aud_interface->pcm_ops->ioctl) {
            ret = aud_interface->pcm_ops->ioctl(substream, cmd, arg);
        } else {
            ret = snd_pcm_lib_ioctl(substream, cmd, arg);
        }
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static int aml_platform_hw_params(struct snd_pcm_substream *substream,
		 struct snd_pcm_hw_params *params)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        BUG_ON(aud_interface->pcm_ops->hw_params == NULL);

        ret = aud_interface->pcm_ops->hw_params(substream, params);
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static int aml_platform_hw_free(struct snd_pcm_substream *substream)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        BUG_ON(aud_interface->pcm_ops->hw_free == NULL);

        ret = aud_interface->pcm_ops->hw_free(substream);
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static int aml_platform_prepare(struct snd_pcm_substream *substream)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        BUG_ON(aud_interface->pcm_ops->prepare == NULL);

        ret = aud_interface->pcm_ops->prepare(substream);
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static int aml_platform_trigger(struct snd_pcm_substream *substream, int cmd)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;


    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        BUG_ON(aud_interface->pcm_ops->trigger == NULL);
        ret = aud_interface->pcm_ops->trigger(substream, cmd);
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static snd_pcm_uframes_t aml_platform_pointer(struct snd_pcm_substream *substream)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    snd_pcm_uframes_t ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        BUG_ON(aud_interface->pcm_ops->pointer == NULL);    
        ret = aud_interface->pcm_ops->pointer(substream);
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
    }

    return ret;
}

static int aml_platform_copy(struct snd_pcm_substream *substream, int channel,
        	    snd_pcm_uframes_t pos,
        	    void __user *buf, snd_pcm_uframes_t count)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        BUG_ON(aud_interface->pcm_ops->copy == NULL);

        ret = aud_interface->pcm_ops->copy(substream, channel, pos, buf, count);
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static int aml_platform_silence(struct snd_pcm_substream *substream, int channel, 
                snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    unsigned char* ppos = NULL;
    ssize_t n = 0;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        if (aud_interface->pcm_ops->silence) {
            ret = aud_interface->pcm_ops->silence(substream, channel, pos, count);
        } else {
            n = frames_to_bytes(runtime, count);
            ppos = runtime->dma_area + frames_to_bytes(runtime, pos);
            memset(ppos, 0, n);
        }
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
        ret = -EINVAL;
    }

    return ret;
}

static struct page *aml_platform_page(struct snd_pcm_substream *substream,
                        unsigned long offset)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    struct page *ret = NULL;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        if (aud_interface->pcm_ops->page) {
            ret = aud_interface->pcm_ops->page(substream, offset);
        }
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
    }

    return ret;
}

static int aml_platform_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        if (aud_interface->pcm_ops->mmap) {
            ret = aud_interface->pcm_ops->mmap(substream, vma);
        }
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
    }

    return ret;
}

static int aml_platform_ack(struct snd_pcm_substream *substream)
{
    struct aml_audio_interface *aud_interface = NULL;
    struct aml_platform_stream *plat_stream = NULL;
    int ret = 0;

    plat_stream = find_platform_stream(substream);
    if (likely(plat_stream != NULL)) {
        aud_interface = plat_stream->interface;
        if (aud_interface->pcm_ops->ack) {
            ret = aud_interface->pcm_ops->ack(substream);
        }
    } else {
        printk(KERN_ERR "aml-platform: substream %p invalid!", substream);
        dump_platform_stream();
    }

    return ret;
}
    
static struct snd_pcm_ops aml_platform_ops = {
	.open		= aml_platform_open,
	.close		= aml_platform_close,
	.ioctl		= aml_platform_ioctl,
	.hw_params	= aml_platform_hw_params,
	.hw_free	= aml_platform_hw_free,
	.prepare	= aml_platform_prepare,
	.trigger	= aml_platform_trigger,
	.pointer	= aml_platform_pointer,
	.copy       = aml_platform_copy,
	.silence    = aml_platform_silence,
	.page       = aml_platform_page,
	.mmap       = aml_platform_mmap,
	.ack        = aml_platform_ack,
};

static int aml_platform_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
	
    struct aml_audio_interface *aud_interface = NULL;
//    struct snd_pcm *pcm =rtd->pcm ;
//    struct snd_soc_card *card = rtd->card;
	
	int ret = 0;
    printk("rtd %x \n",	(unsigned)rtd);
    printk("cpu_dai %x \n",	(unsigned)cpu_dai);
    printk("codec_dai %x \n",	(unsigned)codec_dai);

    printk(KERN_DEBUG"enter %s (codec_dai: %s %d cpu_dai: %s %d)\n",
                    __FUNCTION__, codec_dai->name, codec_dai->id, cpu_dai->name, cpu_dai->id);

    aud_interface = find_audio_interface(cpu_dai->id);
    if (unlikely(NULL == aud_interface)) {
        printk(KERN_ERR "aml-platform: no such audio interface!");
        ret = -ENODEV;
        goto out;
    }

    BUG_ON(aud_interface->pcm_new == NULL);
    ret = aud_interface->pcm_new(rtd);
out:
	return ret;
}

static void aml_platform_pcm_free(struct snd_pcm *pcm)
{
    struct snd_soc_pcm_runtime *rtd = pcm->private_data;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct aml_audio_interface *aud_interface = NULL;

    printk(KERN_DEBUG "enter %s (codec_dai: %s %d cpu_dai: %s %d)\n",
                    __FUNCTION__, codec_dai->name, codec_dai->id, cpu_dai->name, cpu_dai->id);

    aud_interface = find_audio_interface(cpu_dai->id);
    if (unlikely(NULL == aud_interface)) {
        printk(KERN_ERR "aml-platform: no such audio interface!");
        return;
    }

    BUG_ON(aud_interface->pcm_free == NULL);
    aud_interface->pcm_free(pcm);

	return;
}

static int aml_platform_suspend(struct snd_soc_dai *dai)
{

	/* disable the PDC and save the PDC registers */
	// TODO
	printk("aml pcm suspend\n");	

	return 0;
}

static int aml_platform_resume(struct snd_soc_dai *dai)
{
	/* restore the PDC registers and enable the PDC */
	// TODO
	printk("aml pcm resume\n");
	return 0;
}

struct snd_soc_platform_driver aml_soc_platform2 = {
	.ops 	= &aml_platform_ops,
	.pcm_new	= aml_platform_pcm_new,
	.pcm_free	= aml_platform_pcm_free,

	.suspend	= aml_platform_suspend,
	.resume		= aml_platform_resume,
};

static int  aml_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &aml_soc_platform2);
}

static int  aml_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}
#ifdef CONFIG_OF
static const struct of_device_id amlogic_audio_dt_match[]={
	{	.compatible = "amlogic,aml-audio",
	},
	{},
};
#else
#define amlogic_audio_dt_match NULL
#endif

static struct platform_driver aml_soc_platform_driver = {
	.driver = {
			.name = "aml-audio",
			.owner = THIS_MODULE,
			.of_match_table = amlogic_audio_dt_match,			
	},

	.probe = aml_soc_platform_probe,
	.remove = aml_soc_platform_remove,
};

static int __init aml_soc_platform_init(void)
{
	return platform_driver_register(&aml_soc_platform_driver);
}

static void __exit aml_soc_platform_exit(void)
{
    platform_driver_unregister(&aml_soc_platform_driver);
}

module_init(aml_soc_platform_init);

module_exit(aml_soc_platform_exit);

MODULE_AUTHOR("Amlogic, Inc.");
MODULE_DESCRIPTION("Amlogic ASoC platform driver");
MODULE_LICENSE("GPL");

