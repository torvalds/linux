/*
 * amlogic ALSA SoC dummy codec driver
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <linux/of.h>


struct dummy_codec_private {
	struct snd_soc_codec codec;	
};

#define DUMMY_CODEC_RATES		(SNDRV_PCM_RATE_8000_192000)
#define DUMMY_CODEC_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |SNDRV_PCM_FMTBIT_S32_LE)


static int dummy_codec_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	return 0;
}


static int dummy_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	return 0;
}


static int dummy_codec_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static const struct snd_soc_dapm_widget dummy_codec_dapm_widgets[] = {
    /* Output Side */
    /* DACs */
    SND_SOC_DAPM_DAC("Left DAC", "HIFI Playback",
        SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_DAC("Right DAC", "HIFI Playback",
        SND_SOC_NOPM, 7, 0),
        
    /* Output Lines */
    SND_SOC_DAPM_OUTPUT("LOUTL"),
    SND_SOC_DAPM_OUTPUT("LOUTR"),

};

static const struct snd_soc_dapm_route dummy_codec_dapm_routes[] = {
    
    {"LOUTL", NULL, "Left DAC"},
    {"LOUTR", NULL, "Right DAC"},
};
static struct snd_soc_dai_ops dummy_codec_ops = {
	.hw_params		= dummy_codec_pcm_hw_params,
	.set_fmt		= dummy_codec_set_dai_fmt,
	.digital_mute	= dummy_codec_mute,
};

struct snd_soc_dai_driver dummy_codec_dai[] = {
	{
		.name = "dummy_codec",
		.id = 1,
		.playback = {
			.stream_name = "HIFI Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = DUMMY_CODEC_RATES,
			.formats = DUMMY_CODEC_FORMATS,
		},
		.capture = {
			.stream_name = "HIFI Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = DUMMY_CODEC_RATES,
			.formats = DUMMY_CODEC_FORMATS,
		},		
		.ops = &dummy_codec_ops,
	}
};
static int dummy_codec_probe(struct snd_soc_codec *codec)
{
	return 0;
}


static int dummy_codec_remove(struct snd_soc_codec *codec)
{	
	return 0;
};


struct snd_soc_codec_driver soc_codec_dev_dummy_codec = {
    .probe =    dummy_codec_probe,
    .remove =   dummy_codec_remove,
    .dapm_widgets = dummy_codec_dapm_widgets,
    .num_dapm_widgets = ARRAY_SIZE(dummy_codec_dapm_widgets),
    .dapm_routes = dummy_codec_dapm_routes,
    .num_dapm_routes = ARRAY_SIZE(dummy_codec_dapm_routes),
};

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_codec_dt_match[]={
	{	.compatible = "amlogic,aml_dummy_codec",
	},
	{},
};
#else
#define amlogic_codec_dt_match NULL
#endif



static int dummy_codec_platform_probe(struct platform_device *pdev)
{
	struct dummy_codec_private *dummy_codec;
    int ret;
    
    printk("dummy_codec_platform_probe\n");
	dummy_codec = kzalloc(sizeof(struct dummy_codec_private), GFP_KERNEL);
	if (dummy_codec == NULL) {
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, dummy_codec);
    ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_dummy_codec,
			dummy_codec_dai, ARRAY_SIZE(dummy_codec_dai));
    
	if (ret < 0)
		kfree(dummy_codec);
    
	return ret;
}

static int __exit dummy_codec_platform_remove(struct platform_device *pdev)
{
    snd_soc_unregister_codec(&pdev->dev);
	kfree(platform_get_drvdata(pdev));
	return 0;
}

static struct platform_driver dummy_codec_platform_driver = {
	.driver = {
		.name = "dummy_codec",
		.owner = THIS_MODULE,
		.of_match_table = amlogic_codec_dt_match,
		},
	.probe = dummy_codec_platform_probe,
	.remove = dummy_codec_platform_remove,
};

static int __init dummy_codec_init(void)
{
	return platform_driver_register(&dummy_codec_platform_driver);
}

static void __exit dummy_codec_exit(void)
{
	platform_driver_unregister(&dummy_codec_platform_driver);
}

module_init(dummy_codec_init);
module_exit(dummy_codec_exit);

MODULE_AUTHOR("AMLogic, Inc.");
MODULE_DESCRIPTION("ASoC dummy_codec driver");
MODULE_LICENSE("GPL");
