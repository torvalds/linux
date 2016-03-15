/*
 * ALSA SoC pcm5102 codec driver
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

struct pcm5102_private {
	struct snd_soc_codec codec;
};

#define PCM5102_RATES		(SNDRV_PCM_RATE_8000_384000)
#define PCM5102_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |SNDRV_PCM_FMTBIT_S32_LE)

static int pcm5102_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	return 0;
}


static int pcm5102_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	return 0;
}


static int pcm5102_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static const struct snd_soc_dapm_widget pcm5102_dapm_widgets[] = {
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

static const struct snd_soc_dapm_route pcm5102_dapm_routes[] = {
    {"LOUTL", NULL, "Left DAC"},
    {"LOUTR", NULL, "Right DAC"},
};
static struct snd_soc_dai_ops pcm5102_ops = {
	.hw_params		= pcm5102_pcm_hw_params,
	.set_fmt		= pcm5102_set_dai_fmt,
	.digital_mute	= pcm5102_mute,
};

struct snd_soc_dai_driver pcm5102_dai[] = {
	{
		.name = "pcm5102",
		.id = 1,
		.playback = {
			.stream_name = "HIFI Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = PCM5102_RATES,
			.formats = PCM5102_FORMATS,
		},
		.ops = &pcm5102_ops,
	}
};
static int pcm5102_probe(struct snd_soc_codec *codec)
{
	return 0;
}


static int pcm5102_remove(struct snd_soc_codec *codec)
{	
	return 0;
};


struct snd_soc_codec_driver soc_codec_dev_pcm5102 = {
    .probe =    pcm5102_probe,
    .remove =   pcm5102_remove,
    .dapm_widgets = pcm5102_dapm_widgets,
    .num_dapm_widgets = ARRAY_SIZE(pcm5102_dapm_widgets),
    .dapm_routes = pcm5102_dapm_routes,
    .num_dapm_routes = ARRAY_SIZE(pcm5102_dapm_routes),
};

#ifdef CONFIG_USE_OF
static const struct of_device_id codec_dt_match[]={
	{	.compatible = "hardkernel,pcm5102",
	},
	{},
};
#else
#define codec_dt_match NULL
#endif



static int pcm5102_platform_probe(struct platform_device *pdev)
{
	struct pcm5102_private *pcm5102;
	int ret;
    
	printk("pcm5102_platform_probe\n");
	pcm5102 = devm_kzalloc(&pdev->dev, sizeof(struct pcm5102_private), GFP_KERNEL);
	if (pcm5102 == NULL) {
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, pcm5102);
	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_pcm5102,
			pcm5102_dai, ARRAY_SIZE(pcm5102_dai));
	return ret;
}

static int __exit pcm5102_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver pcm5102_platform_driver = {
	.driver = {
		.name = "pcm5102",
		.owner = THIS_MODULE,
		.of_match_table = codec_dt_match,
		},
	.probe = pcm5102_platform_probe,
	.remove = pcm5102_platform_remove,
};

static int __init pcm5102_init(void)
{
	return platform_driver_register(&pcm5102_platform_driver);
}

static void __exit pcm5102_exit(void)
{
	platform_driver_unregister(&pcm5102_platform_driver);
}

module_init(pcm5102_init);
module_exit(pcm5102_exit);

MODULE_AUTHOR("HardKernel, Inc.");
MODULE_DESCRIPTION("ASoC pcm5102 driver");
MODULE_LICENSE("GPL");
