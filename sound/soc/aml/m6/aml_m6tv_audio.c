/*
	amlogic  M6TV sound card machine   driver code.
	it support multi-codec on board, one codec as the main codec,others as
	aux devices.
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <linux/switch.h>
#include "aml_dai.h"
#include "aml_pcm.h"
#include "aml_audio_hw.h"
#include <sound/aml_m6tv_audio.h>
#include "aml_audio_codec_probe.h"

#ifdef CONFIG_USE_OF
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of_gpio.h>
#include <mach/pinmux.h>
#include <plat/io.h>
#endif

static struct platform_device *m6tv_audio_snd_device = NULL;
static struct m6tv_audio_codec_platform_data *m6tv_audio_snd_pdata = NULL;
//static struct m6tv_audio_private_data* m6tv_audio_snd_priv = NULL;
struct aml_audio_private_data {
	struct pinctrl *pin_ctl;
	int gpio_mute;
	bool mute_inv;
};

#define CODEC_DEBUG  printk

static void m6tv_audio_dev_uninit(void)
{
    if (m6tv_audio_snd_pdata->device_uninit) {
        m6tv_audio_snd_pdata->device_uninit();
    }
}
static int m6tv_audio_prepare(struct snd_pcm_substream *substream)
{
    CODEC_DEBUG( "enter %s stream: %s\n", __func__, (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture");
    return 0;
}
static int m6tv_audio_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    int ret;
    CODEC_DEBUG( "enter %s stream: %s rate: %d format: %d\n", __func__, (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture", params_rate(params), params_format(params));

    /* set codec DAI configuration */
    ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
    if (ret < 0) {
        CODEC_DEBUG(KERN_ERR "%s: set codec dai fmt failed!\n", __func__);
        return ret;
    }

    /* set cpu DAI configuration */
    ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
    if (ret < 0) {
        CODEC_DEBUG(KERN_ERR "%s: set cpu dai fmt failed!\n", __func__);
        return ret;
    }

    /* set codec DAI clock */
    ret = snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * 512, SND_SOC_CLOCK_IN);
    if (ret < 0) {
        CODEC_DEBUG(KERN_ERR "%s: set codec dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    /* set cpu DAI clock */
    ret = snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * 512, SND_SOC_CLOCK_OUT);
    if (ret < 0) {
        CODEC_DEBUG(KERN_ERR "%s: set cpu dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    return 0;
}
static struct snd_soc_ops m6tv_audio_soc_ops = {
    .prepare   = m6tv_audio_prepare,
    .hw_params = m6tv_audio_hw_params,
};


static int m6tv_audio_set_bias_level(struct snd_soc_card *card,
						struct snd_soc_dapm_context *dapm,
						enum snd_soc_bias_level level)
{
	int ret = 0;
    	CODEC_DEBUG( "enter %s level: %d\n", __func__, level);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int m6tv_audio_suspend_pre(struct snd_soc_card *card)
{
    CODEC_DEBUG( "enter %s\n", __func__);
    return 0;
}

static int m6tv_audio_suspend_post(struct snd_soc_card *card)
{
    CODEC_DEBUG( "enter %s\n", __func__);
    return 0;
}

static int m6tv_audio_resume_pre(struct snd_soc_card *card)
{
    CODEC_DEBUG( "enter %s\n", __func__);
    return 0;
}

static int m6tv_audio_resume_post(struct snd_soc_card *card)
{
    CODEC_DEBUG( "enter %s\n", __func__);
    return 0;
}
#else
#define m6tv_audio_suspend_pre  NULL
#define m6tv_audio_suspend_post NULL
#define m6tv_audio_resume_pre   NULL
#define m6tv_audio_resume_post  NULL
#endif

static int m6tv_audio_codec_init(struct snd_soc_pcm_runtime *rtd)
{
    return 0;
}

static struct snd_soc_dai_link m6tv_audio_dai_link[] = {
    {
        .name = "syno9629",
        .stream_name = "SYNO9629 PCM",
        .cpu_dai_name = "aml-dai0",
        .codec_dai_name = "syno9629-hifi",
        .init = m6tv_audio_codec_init,
        .platform_name = "aml-audio.0",
        .codec_name = "syno9629.0",
        .ops = &m6tv_audio_soc_ops,
    },
};
struct snd_soc_aux_dev m6tv_audio_aux_dev;

static struct snd_soc_codec_conf m6tv_audio_codec_conf[] = {
	{
		.name_prefix = "AMP",
	},
};
static struct snd_soc_card snd_soc_m6tv_audio = {
    .name = "AML-M6TV",
    .driver_name = "SOC-Audio",
    .dai_link = m6tv_audio_dai_link,
    .num_links = ARRAY_SIZE(m6tv_audio_dai_link),
    .set_bias_level = m6tv_audio_set_bias_level,
    .aux_dev = &m6tv_audio_aux_dev,
    .num_aux_devs = 1,
    .codec_conf = m6tv_audio_codec_conf,
    .num_configs = ARRAY_SIZE(m6tv_audio_codec_conf),

#ifdef CONFIG_PM_SLEEP
	.suspend_pre    = m6tv_audio_suspend_pre,
	.suspend_post   = m6tv_audio_suspend_post,
	.resume_pre     = m6tv_audio_resume_pre,
	.resume_post    = m6tv_audio_resume_post,
#endif
};

static void aml_m6_pinmux_init(struct snd_soc_card *card)
{
	struct aml_audio_private_data *p_aml_audio;
	const char *str=NULL;
	int ret = 0;

	p_aml_audio = snd_soc_card_get_drvdata(card);
	p_aml_audio->pin_ctl = devm_pinctrl_get_select(card->dev, "aml_m6tv_audio");

	ret = of_property_read_string(card->dev->of_node, "mute_gpio", &str);
	if (ret < 0) {
		printk("aml_snd_m6tv: failed to get mute_gpio!\n");
	}else{
		p_aml_audio->gpio_mute = amlogic_gpio_name_map_num(str);
		p_aml_audio->mute_inv = of_property_read_bool(card->dev->of_node,"mute_inv");
		amlogic_gpio_request_one(p_aml_audio->gpio_mute,GPIOF_OUT_INIT_LOW,"mute_spk");
		amlogic_set_value(p_aml_audio->gpio_mute, 1, "mute_spk");
	}


	printk("=%s=,aml_m6tv_pinmux_init done\n",__func__);
}

static void aml_m6_pinmux_deinit(struct snd_soc_card *card)
{
	struct aml_audio_private_data *p_aml_audio;

	p_aml_audio = snd_soc_card_get_drvdata(card);

	//amlogic_gpio_free(p_aml_audio->gpio_hp_det,"rt5631");
	devm_pinctrl_put(p_aml_audio->pin_ctl);
}


static int m6tv_audio_audio_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct snd_soc_card *card = &snd_soc_m6tv_audio;
	struct aml_audio_private_data *p_aml_audio;

	CODEC_DEBUG( "enter %s\n", __func__);
	p_aml_audio = devm_kzalloc(&pdev->dev,
			sizeof(struct aml_audio_private_data), GFP_KERNEL);
	if (!p_aml_audio) {
		dev_err(&pdev->dev, "Can't allocate aml_audio_private_data\n");
		ret = -ENOMEM;
		goto err;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, p_aml_audio);
	if (!(pdev->dev.of_node)) {
		dev_err(&pdev->dev, "Must be instantiated using device tree\n");
		ret = -EINVAL;
		goto err;
	}

	m6tv_audio_aux_dev.name = codec_info.name;
	m6tv_audio_aux_dev.codec_name = codec_info.name_bus;
	m6tv_audio_codec_conf[0].dev_name = codec_info.name_bus;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err;
	}

	aml_m6_pinmux_init(card);

err:
//	kfree(m6tv_audio_snd_priv);
	return ret;
}

static int m6tv_audio_audio_remove(struct platform_device *pdev)
{
    int ret = 0;
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	aml_m6_pinmux_deinit(card);
    m6tv_audio_dev_uninit();
    platform_device_put(m6tv_audio_snd_device);
//    kfree(m6tv_audio_snd_priv);
    m6tv_audio_snd_device = NULL;
//    m6tv_audio_snd_priv = NULL;
    m6tv_audio_snd_pdata = NULL;
    return ret;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_audio_dt_match[]={
	{	.compatible = "sound_card,aml_m6tv_audio",
	},
	{},
};
#else
#define amlogic_audio_dt_match NULL
#endif

static struct platform_driver aml_m6tv_audio_driver = {
    .probe  = m6tv_audio_audio_probe,
    .remove = m6tv_audio_audio_remove,
    .driver = {
        .name = "aml_m6tv_audio",
        .owner = THIS_MODULE,
        .of_match_table = amlogic_audio_dt_match,
    },
};

static int __init aml_m6tv_audio_init(void)
{
	CODEC_DEBUG( "enter %s\n", __func__);
	return platform_driver_register(&aml_m6tv_audio_driver);
}

static void __exit aml_m6tv_audio_exit(void)
{
    platform_driver_unregister(&aml_m6tv_audio_driver);
}

module_init(aml_m6tv_audio_init);
module_exit(aml_m6tv_audio_exit);

/* Module information */
MODULE_AUTHOR("jian.xu@amlogic.com AMLogic, Inc.");
MODULE_DESCRIPTION("AML SYNO9629 ALSA machine layer driver");
MODULE_LICENSE("GPL");

