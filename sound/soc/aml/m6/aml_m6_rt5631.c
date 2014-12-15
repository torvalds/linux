/*
 * aml_m6_asoc_audio.c  --  SoC audio for AML M6
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
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
#include <sound/rt5631.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <linux/switch.h>

//#include "../codecs/rt5631.h"
#include "aml_dai.h"
#include "aml_pcm.h"
#include "aml_audio_hw.h"

#ifdef CONFIG_USE_OF
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of_gpio.h>
#include <mach/pinmux.h>
#include <plat/io.h>
#endif

#define DRV_NAME "aml_rt5631_card"
#define HP_DET                  1
#define HP_IRQ                  0
struct aml_audio_private_data {
    int bias_level;
    int clock_en;
	int gpio_hp_det;
	bool det_pol_inv;
	struct pinctrl *pin_ctl;

    int timer_en;
    int detect_flag;
    struct timer_list timer;
    struct work_struct work;
    struct mutex lock;
    struct snd_soc_jack jack;
    void* data;

	struct switch_dev sdev; // for android
};

static void aml_set_clock(int enable)
{
    /* set clock gating */
    //p_aml_audio->clock_en = enable;

    return ;
}

#if HP_DET
static void aml_audio_start_timer(struct aml_audio_private_data *p_aml_audio, unsigned long delay)
{
    p_aml_audio->timer.expires = jiffies + delay;
    p_aml_audio->timer.data = (unsigned long)p_aml_audio;
    p_aml_audio->detect_flag = -1;
    add_timer(&p_aml_audio->timer);
    p_aml_audio->timer_en = 1;
}

static void aml_audio_stop_timer(struct aml_audio_private_data *p_aml_audio)
{
    del_timer_sync(&p_aml_audio->timer);
    cancel_work_sync(&p_aml_audio->work);
    p_aml_audio->timer_en = 0;
    p_aml_audio->detect_flag = -1;
}

static int aml_audio_hp_detect(struct aml_audio_private_data *p_aml_audio)
{
	int val = amlogic_get_value(p_aml_audio->gpio_hp_det,"rt5631");
	return p_aml_audio->det_pol_inv ? (!val):val; 
}


static void aml_asoc_work_func(struct work_struct *work)
{
    struct aml_audio_private_data *p_aml_audio = NULL;
    struct snd_soc_card *card = NULL;
    int jack_type = 0;
    int flag = -1;
	int status = SND_JACK_HEADPHONE;
    p_aml_audio = container_of(work, struct aml_audio_private_data, work);
    card = (struct snd_soc_card *)p_aml_audio->data;

    flag = aml_audio_hp_detect(p_aml_audio);

    if(p_aml_audio->detect_flag != flag) {
        if (flag == 1) {
			switch_set_state(&p_aml_audio->sdev, 2);  // 1 :have mic ;  2 no mic
            printk(KERN_INFO "aml aduio hp pluged jack_type: %d\n", jack_type);
            snd_soc_jack_report(&p_aml_audio->jack, status, SND_JACK_HEADPHONE);
        } else {
            printk(KERN_INFO "aml audio hp unpluged\n");
			switch_set_state(&p_aml_audio->sdev, 0);
            snd_soc_jack_report(&p_aml_audio->jack, 0, SND_JACK_HEADPHONE);
        }

        p_aml_audio->detect_flag = flag;
    }
}


static void aml_asoc_timer_func(unsigned long data)
{
    struct aml_audio_private_data *p_aml_audio = (struct aml_audio_private_data *)data;
    unsigned long delay = msecs_to_jiffies(200);

    schedule_work(&p_aml_audio->work);
    mod_timer(&p_aml_audio->timer, jiffies + delay);
}
#endif

static int aml_asoc_hw_params(struct snd_pcm_substream *substream,
    struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    int ret;

    printk(KERN_DEBUG "enter %s stream: %s rate: %d format: %d\n", __func__, (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture", params_rate(params), params_format(params));

    /* set codec DAI configuration */
    ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
    if (ret < 0) {
        printk(KERN_ERR "%s: set codec dai fmt failed!\n", __func__);
        return ret;
    }

    /* set cpu DAI configuration */
    ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
    if (ret < 0) {
        printk(KERN_ERR "%s: set cpu dai fmt failed!\n", __func__);
        return ret;
    }

    /* set codec DAI clock */
    ret = snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * 512, SND_SOC_CLOCK_IN);
    if (ret < 0) {
        printk(KERN_ERR "%s: set codec dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    /* set cpu DAI clock */
    ret = snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * 512, SND_SOC_CLOCK_OUT);
    if (ret < 0) {
        printk(KERN_ERR "%s: set cpu dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    return 0;
}

static struct snd_soc_ops aml_asoc_ops = {
    .hw_params = aml_asoc_hw_params,
};

static int aml_set_bias_level(struct snd_soc_card *card,
		struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
    int ret = 0;
	struct aml_audio_private_data * p_aml_audio;
	p_aml_audio = snd_soc_card_get_drvdata(card);
	printk(KERN_DEBUG "enter %s level: %d\n", __func__, level);

    if (p_aml_audio->bias_level == (int)level)
        return 0;

    switch (level) {
    case SND_SOC_BIAS_ON:
        break;
    case SND_SOC_BIAS_PREPARE:
        /* clock enable */
        if (!p_aml_audio->clock_en) {
            aml_set_clock(1);
        }
        break;

    case SND_SOC_BIAS_OFF:
        if (p_aml_audio->clock_en) {
            aml_set_clock(0);
        }

        break;
    case SND_SOC_BIAS_STANDBY:
        /* clock disable */
        if (p_aml_audio->clock_en) {
            aml_set_clock(0);
        }

        break;
    default:
        return ret;
    }

    p_aml_audio->bias_level = (int)level;

    return ret;
}

#ifdef CONFIG_PM_SLEEP
static int aml_suspend_pre(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
#if 0//HP_DET
    /* stop timer */
    mutex_lock(&p_aml_audio->lock);
    if (p_aml_audio->timer_en) {
       // rt5631_stop_timer();
    }
    mutex_unlock(&p_aml_audio->lock);
#endif
    return 0;
}

static int aml_suspend_post(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}

static int aml_resume_pre(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}

static int aml_resume_post(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}
#else
#define aml_suspend_pre  NULL
#define aml_suspend_post NULL
#define aml_resume_pre   NULL
#define aml_resume_post  NULL
#endif

static const struct snd_soc_dapm_widget aml_asoc_dapm_widgets[] = {
    SND_SOC_DAPM_SPK("Ext Spk", NULL),
    SND_SOC_DAPM_HP("HP", NULL),
    SND_SOC_DAPM_MIC("MAIN MIC", NULL),
    SND_SOC_DAPM_MIC("HEADSET MIC", NULL),
};

static struct snd_soc_jack_pin jack_pins[] = {
    {
        .pin = "HP",
        .mask = SND_JACK_HEADPHONE,
    }
};
#if HP_IRQ
static struct snd_soc_jack_gpio aml_audio_hp_jack_gpio = {
	.name = "Headset detection",
	.report = SND_JACK_HEADSET,
	.debounce_time = 150,
};
#endif
static int aml_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
    struct snd_soc_codec *codec = rtd->codec;
    //struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct aml_audio_private_data * p_aml_audio;
    int ret = 0;
	
    printk(KERN_DEBUG "enter %s \n", __func__);
	p_aml_audio = snd_soc_card_get_drvdata(card);

    /* Add specific widgets */
    snd_soc_dapm_new_controls(dapm, aml_asoc_dapm_widgets,
                  ARRAY_SIZE(aml_asoc_dapm_widgets));
    ret = snd_soc_jack_new(codec, "hp switch", SND_JACK_HEADPHONE, &p_aml_audio->jack);
    if (ret) {
        printk(KERN_WARNING "Failed to alloc resource for hp switch\n");
    } else {
        ret = snd_soc_jack_add_pins(&p_aml_audio->jack, ARRAY_SIZE(jack_pins), jack_pins);
        if (ret) {
            printk(KERN_WARNING "Failed to setup hp pins\n");
        }
    }
#if HP_IRQ	
	p_aml_audio->gpio_hp_det = of_get_named_gpio(card->dev->of_node,"rt5631_gpio",0);

	if (gpio_is_valid(p_aml_audio->gpio_hp_det)) {
		aml_audio_hp_jack_gpio.gpio = p_aml_audio->gpio_hp_det;
		snd_soc_jack_add_gpios(&p_aml_audio->jack,
						1, &aml_audio_hp_jack_gpio);
	}
#endif
#if HP_DET
    init_timer(&p_aml_audio->timer);
    p_aml_audio->timer.function = aml_asoc_timer_func;
    p_aml_audio->timer.data = (unsigned long)p_aml_audio;
    p_aml_audio->data= (void*)card;

    INIT_WORK(&p_aml_audio->work, aml_asoc_work_func);
    mutex_init(&p_aml_audio->lock);

    mutex_lock(&p_aml_audio->lock);
    if (!p_aml_audio->timer_en) {
        aml_audio_start_timer(p_aml_audio, msecs_to_jiffies(100));
    }
    mutex_unlock(&p_aml_audio->lock);

#endif

    return 0;
}

static struct snd_soc_dai_link aml_codec_dai_link[] = {
    {
        .name = "RT5631",
        .stream_name = "AML PCM",
        .cpu_dai_name = "aml-dai0",
        //.codec_dai_name = "rt5631-hifi",
        .init = aml_asoc_init,
        .platform_name = "aml-audio.0",
        .codec_name = "rt5631.2-001a",
        .ops = &aml_asoc_ops,
    },
};

static struct snd_soc_card aml_snd_soc_card = {
    //.name = "AML-RT5631",
    .driver_name = "SOC-Audio",
    .dai_link = &aml_codec_dai_link[0],
    .num_links = ARRAY_SIZE(aml_codec_dai_link),
    .set_bias_level = aml_set_bias_level,
#ifdef CONFIG_PM_SLEEP
	.suspend_pre    = aml_suspend_pre,
	.suspend_post   = aml_suspend_post,
	.resume_pre     = aml_resume_pre,
	.resume_post    = aml_resume_post,
#endif
};

static void aml_m6_pinmux_init(struct snd_soc_card *card)
{
	struct aml_audio_private_data *p_aml_audio;
	const char *str;
	int ret;
	p_aml_audio = snd_soc_card_get_drvdata(card);
	p_aml_audio->pin_ctl = devm_pinctrl_get_select(card->dev, "rt5631_audio");
#if HP_DET
	ret = of_property_read_string(card->dev->of_node, "rt5631_gpio", &str);
	if (ret) {
		printk("rt5631: faild to get gpio!\n");
	}
	p_aml_audio->gpio_hp_det = amlogic_gpio_name_map_num(str);
//	p_aml_audio->gpio_hp_det = of_get_named_gpio(card->dev->of_node,"rt5631_gpio",0);
	p_aml_audio->det_pol_inv = of_property_read_bool(card->dev->of_node,"hp_det_inv");
	amlogic_gpio_request_one(p_aml_audio->gpio_hp_det,GPIOF_IN,"rt5631");
#endif
	printk("=%s==,aml_m6_pinmux_init done,---%d\n",__func__,p_aml_audio->det_pol_inv);
}

static void aml_m6_pinmux_deinit(struct snd_soc_card *card)
{
	struct aml_audio_private_data *p_aml_audio;

	p_aml_audio = snd_soc_card_get_drvdata(card);

	amlogic_gpio_free(p_aml_audio->gpio_hp_det,"rt5631");
	devm_pinctrl_put(p_aml_audio->pin_ctl);
}
static int aml_m6_audio_probe(struct platform_device *pdev)
{
	//struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &aml_snd_soc_card;
	struct aml_audio_private_data *p_aml_audio;
    int ret = 0;

    printk(KERN_DEBUG "enter %s\n", __func__);

#ifdef CONFIG_USE_OF
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

	ret = snd_soc_of_parse_card_name(card, "aml,sound_card");
	if (ret)
		goto err;
	
	ret = of_property_read_string_index(pdev->dev.of_node, "aml,codec_dai",
			0, &aml_codec_dai_link[0].codec_dai_name);
	if (ret)
		goto err;

	ret = snd_soc_of_parse_audio_routing(card, "aml,audio-routing");
	if (ret)
		goto err;

//	aml_codec_dai_link[0].codec_of_node = of_parse_phandle(
//			pdev->dev.of_node, "aml,audio-codec", 0);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err;
	}

	aml_m6_pinmux_init(card);

	p_aml_audio->sdev.name = "h2w";//for report headphone to android
	ret = switch_dev_register(&p_aml_audio->sdev);
	if (ret < 0){
			printk(KERN_ERR "ASoC: register switch dev failed\n");
			goto err;
	}

	return 0;
#endif

err:
    kfree(p_aml_audio);
    return ret;
}

static int aml_m6_audio_remove(struct platform_device *pdev)
{
    int ret = 0;
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct aml_audio_private_data *p_aml_audio;
	p_aml_audio = snd_soc_card_get_drvdata(card);
#if HP_IRQ

	snd_soc_jack_free_gpios(&p_aml_audio->jack, 1,//
			&aml_audio_hp_jack_gpio);//
#endif
	snd_soc_unregister_card(card);
#if HP_DET
	/* stop timer */
	mutex_lock(&p_aml_audio->lock);
	if (p_aml_audio->timer_en) {
		aml_audio_stop_timer(p_aml_audio);
	}
	mutex_unlock(&p_aml_audio->lock);
#endif

	aml_m6_pinmux_deinit(card);
    kfree(p_aml_audio);
    return ret;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_audio_dt_match[]={
	{	.compatible = "sound_card,rt5631",//"amlogic,aml_rt5631_audio",
	},
	{},
};
#else
#define amlogic_audio_dt_match NULL
#endif

static struct platform_driver aml_m6_rt5631_audio_driver = {
    .probe  = aml_m6_audio_probe,
    .remove = aml_m6_audio_remove,
    .driver = {
        .name = DRV_NAME,//"aml_rt5631_audio",
        .owner = THIS_MODULE,
        .pm = &snd_soc_pm_ops,
        .of_match_table = amlogic_audio_dt_match,
    },
};

static int __init aml_m6_rt5631_audio_init(void)
{
    return platform_driver_register(&aml_m6_rt5631_audio_driver);
}

static void __exit aml_m6_rt5631_audio_exit(void)
{
    platform_driver_unregister(&aml_m6_rt5631_audio_driver);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(aml_m6_rt5631_audio_init);
#else
module_init(aml_m6_rt5631_audio_init);
#endif
module_exit(aml_m6_rt5631_audio_exit);

/* Module information */
MODULE_AUTHOR("AMLogic, Inc.");
MODULE_DESCRIPTION("AML_M6 audio machine Asoc driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, amlogic_audio_dt_match);

