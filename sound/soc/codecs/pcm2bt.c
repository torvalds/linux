/*
 * PCM2BT ALSA SoC Audio driver
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>


#include "pcm2bt.h"

struct pcm2bt_priv {	
    struct snd_soc_codec codec;
};


#define PCM2BT_RATES SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000

#define PCM2BT_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S8)
	
static int pcm2bt_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
    return 0;
}

static int pcm2bt_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
    return 0;
}

static int pcm2bt_set_sysclk(struct snd_soc_dai *dai,   int clk_id, 
        unsigned int freq, int dir)
{    
    return 0;
}

static int pcm2bt_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
    return 0;
}

static int pcm2bt_set_bias_level(struct snd_soc_codec *codec,	
        enum snd_soc_bias_level level)
{  
   return 0;
}

struct snd_soc_dai_ops pcm2bt_dai_ops = {
	.hw_params = pcm2bt_hw_params,
	.set_fmt = pcm2bt_set_fmt,
	.set_sysclk = pcm2bt_set_sysclk,
	.shutdown = pcm2bt_shutdown,
	.set_sysclk = pcm2bt_set_sysclk,
};

struct snd_soc_dai_driver pcm2bt_dai[] = {
    {
		.name = "pcm2bt-pcm",
		.playback = {
			.stream_name = "PCM2BT Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = PCM2BT_RATES,
			.formats = PCM2BT_FORMATS,
		},
		.capture = {
			.stream_name = "BT2PCM Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = PCM2BT_RATES,
			.formats = PCM2BT_FORMATS,
		},
		.ops = &pcm2bt_dai_ops,
		.symmetric_rates = 1,
    },
};

static int pcm2bt_probe(struct snd_soc_codec *codec)
{
    return 0;
}

static int pcm2bt_remove(struct snd_soc_codec *codec)
{   
    return 0;
}

static int pcm2bt_suspend(struct snd_soc_codec *codec,
        pm_message_t state)
{
    return 0;
}
static int pcm2bt_resume(struct snd_soc_codec *codec)
{
    return 0;
}


static struct snd_soc_codec_driver soc_codec_dev_pcm2bt = {
	.probe = pcm2bt_probe,
	.remove = pcm2bt_remove,
	.suspend = pcm2bt_suspend,
	.resume = pcm2bt_resume,
	.set_bias_level = pcm2bt_set_bias_level,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_pcm2bt);

static int pcm2bt_platform_probe(struct platform_device *pdev)
{
    int ret;
    struct pcm2bt_priv *pcm2bt = NULL;
   printk("*****enter pcm2bt_codec_probe\n");
    ret = snd_soc_register_codec(&pdev->dev, 
        &soc_codec_dev_pcm2bt, pcm2bt_dai, ARRAY_SIZE(pcm2bt_dai));    
   

    printk("pcm2bt_codec_probe ok!\n");
    
    return ret;
    
}

static int pcm2bt_platform_remove(struct platform_device *pdev)
{   
    snd_soc_unregister_codec(&pdev->dev);
    return 0;   
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_pcm2BT_codec_dt_match[]={
    { .compatible = "amlogic,pcm2BT-codec", },
    {},
};
#else
#define amlogic_pcm2BT_codec_dt_match NULL
#endif


static struct platform_driver pcm2bt_platform_driver = {
	.driver = {
		.name = "pcm2bt",
		.owner = THIS_MODULE,
		.of_match_table = amlogic_pcm2BT_codec_dt_match,
		},
	.probe = pcm2bt_platform_probe,
	.remove = pcm2bt_platform_remove,
};

static int __init pcm_bt_init(void)
{
	return platform_driver_register(&pcm2bt_platform_driver);
}
module_init(pcm_bt_init);

static void __exit pcm_bt_exit(void)
{
	platform_driver_unregister(&pcm2bt_platform_driver);
}
module_exit(pcm_bt_exit);

MODULE_DESCRIPTION("ASoC pcm2bt driver");
MODULE_AUTHOR("AMLogic, Inc.");
MODULE_LICENSE("GPL");
