/*
 * aml_m8.c  --  SoC audio for AML M8
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
#include <linux/delay.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>


#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <linux/switch.h>
#include <linux/amlogic/saradc.h>

#include "aml_i2s_dai.h"
#include "aml_i2s.h"
#include "aml_audio_hw.h"
#include "aml_m8.h"

#include <mach/register.h>

#ifdef CONFIG_USE_OF
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of_gpio.h>
#include <mach/pinmux.h>
#include <plat/io.h>
#endif

#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif

#define USE_EXTERNAL_DAC 1
#define DRV_NAME "aml_m8_rt5616"
#define HP_DET                  1

int spk_gpio_mute;

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

static int hp_det_adc_value(struct aml_audio_private_data *p_aml_audio)
{
    int ret,hp_value,hp_val_sum,loop_num;
    hp_val_sum = 0;
    loop_num = 0;
    unsigned int mic_ret = 0;
    
    while(loop_num < 8){
        hp_value = get_adc_sample(p_aml_audio->hp_adc_ch);
        if(hp_value <0){
            printk("hp detect get error adc value!\n");
            continue;
        }
        hp_val_sum += hp_value;
        loop_num ++;
        msleep(15);
    }
    hp_val_sum = hp_val_sum >> 3;

    if(hp_val_sum >= p_aml_audio->hp_val_h){
        ret = 0;
    }else if((hp_val_sum <= (p_aml_audio->hp_val_l))&& hp_val_sum >=0){
        ret = 1;
        if(p_aml_audio->mic_det){
            if(hp_val_sum <=  p_aml_audio->mic_val){
                mic_ret = 8;
                ret |= mic_ret;
            }
        }
    }else{
        ret = 2;
        if(p_aml_audio->mic_det){
            ret = 0;
            mic_ret = 8;
            ret |= mic_ret; 
        }
            
    }
    
    return ret;
}


static int aml_audio_hp_detect(struct aml_audio_private_data *p_aml_audio)
{
       // return 0;
   int loop_num = 0;
   int ret;

    mutex_lock(&p_aml_audio->lock);

    while(loop_num < 2){
        ret = hp_det_adc_value(p_aml_audio);
        if(p_aml_audio->hp_last_state != ret){
            loop_num = 0;
            msleep(30);
            if(ret < 0){
                ret = p_aml_audio->hp_last_state;
            }else {
                p_aml_audio->hp_last_state = ret;
            }
        }else{
            msleep(30);
            loop_num = loop_num + 1;
        }
    }
 
    mutex_unlock(&p_aml_audio->lock);

    return ret; 
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

        p_aml_audio->detect_flag = flag;
        
        if (flag & 0x1) {
            //amlogic_set_value(p_aml_audio->gpio_mute, 0, "mute_spk");
            switch_set_state(&p_aml_audio->sdev, 2);  // 1 :have mic ;  2 no mic
            //adac_wr_reg (71, 0x0101); // use board mic
            printk(KERN_INFO "aml aduio hp pluged 3 jack_type: %d\n", SND_JACK_HEADPHONE);
            snd_soc_jack_report(&p_aml_audio->jack, status, SND_JACK_HEADPHONE);

           // mic port detect
           if(p_aml_audio->mic_det){
               if(flag & 0x8){
                  switch_set_state(&p_aml_audio->mic_sdev, 1);
                 // adac_wr_reg (71, 0x0005); // use hp mic
                  printk(KERN_INFO "aml aduio mic pluged jack_type: %d\n", SND_JACK_MICROPHONE);
                  snd_soc_jack_report(&p_aml_audio->jack, status, SND_JACK_HEADPHONE);
              }
           }

        } else if(flag & 0x2){
            //amlogic_set_value(p_aml_audio->gpio_mute, 0, "mute_spk");
            switch_set_state(&p_aml_audio->sdev, 1);  // 1 :have mic ;  2 no mic
           // adac_wr_reg (71, 0x0005); // use hp mic
            printk(KERN_INFO "aml aduio hp pluged 4 jack_type: %d\n", SND_JACK_HEADSET);
            snd_soc_jack_report(&p_aml_audio->jack, status, SND_JACK_HEADPHONE);
        } else {
            printk(KERN_INFO "aml audio hp unpluged\n");
           // amlogic_set_value(p_aml_audio->gpio_mute, 1, "mute_spk");
//            adac_wr_reg (71, 0x0101); // use board mic
            switch_set_state(&p_aml_audio->sdev, 0);
            snd_soc_jack_report(&p_aml_audio->jack, 0, SND_JACK_HEADPHONE);

            // mic port detect
            if(p_aml_audio->mic_det){
                if(flag & 0x8){
                   switch_set_state(&p_aml_audio->mic_sdev, 1);
                   //adac_wr_reg (71, 0x0005); // use hp mic
                   printk(KERN_INFO "aml aduio mic pluged jack_type: %d\n", SND_JACK_MICROPHONE);
                   snd_soc_jack_report(&p_aml_audio->jack, status, SND_JACK_HEADPHONE);
               }
            }
        }
        
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
        SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBM_CFM);
    if (ret < 0) {
        printk(KERN_ERR "%s: set cpu dai fmt failed!\n", __func__);
        return ret;
    }
#if 1
    /* set codec DAI clock */
    ret = snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * 256, SND_SOC_CLOCK_IN);
    if (ret < 0) {
        printk(KERN_ERR "%s: set codec dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    /* set cpu DAI clock */
    ret = snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * 256, SND_SOC_CLOCK_OUT);
    if (ret < 0) {
        printk(KERN_ERR "%s: set cpu dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }
#endif
    return 0;
}

static struct snd_soc_ops aml_asoc_ops = {
    .hw_params = aml_asoc_hw_params,
};


//static struct aml_audio_private_data *p_audio;

static int aml_m8_spk_enabled;

static int aml_m8_set_spk(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *ucontrol)
{
    return 0;
    aml_m8_spk_enabled = ucontrol->value.integer.value[0];
    printk(KERN_INFO "aml_m8_set_spk: aml_m8_spk_enabled=%d\n",aml_m8_spk_enabled);

    msleep(10);
    amlogic_set_value(spk_gpio_mute, aml_m8_spk_enabled, "mute_spk");

    if(aml_m8_spk_enabled ==1)
        msleep(100);

    return 0;
}

static int aml_m8_get_spk(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *ucontrol)
{
    //printk(KERN_INFO"aml_m8_get_spk:aml_m8_spk_enabled=%d\n",aml_m8_spk_enabled);
    ucontrol->value.integer.value[0] = aml_m8_spk_enabled;
    return 0;
}


static int aml_set_bias_level(struct snd_soc_card *card,
        struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
    int ret = 0;
    struct aml_audio_private_data * p_aml_audio;
    p_aml_audio = snd_soc_card_get_drvdata(card);
    printk(KERN_DEBUG "enter %s level: %d\n", __func__, level);

    int hp_state = p_aml_audio->detect_flag;
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
#if HP_DET

#endif
    return 0;
}

static int i2s_gpio_set(struct snd_soc_card *card)
{
    struct aml_audio_private_data *p_aml_audio;
    const char *str=NULL;
    int ret;
    

    p_aml_audio = snd_soc_card_get_drvdata(card);
    if(p_aml_audio->pin_ctl)
        devm_pinctrl_put(p_aml_audio->pin_ctl);
    ret = of_property_read_string(card->dev->of_node, "I2S_MCLK", &str);
    if (ret < 0) {
        printk("I2S_MCLK: faild to get gpio I2S_MCLK!\n");
    }else{
        p_aml_audio->gpio_i2s_m = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_aml_audio->gpio_i2s_m,GPIOF_OUT_INIT_LOW,"low_mclk");
        amlogic_set_value(p_aml_audio->gpio_i2s_m, 0, "low_mclk");
    }

    ret = of_property_read_string(card->dev->of_node, "I2S_SCLK", &str);
    if (ret < 0) {
        printk("I2S_SCLK: faild to get gpio I2S_SCLK!\n");
    }else{
        p_aml_audio->gpio_i2s_s = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_aml_audio->gpio_i2s_s,GPIOF_OUT_INIT_LOW,"low_sclk");
        amlogic_set_value(p_aml_audio->gpio_i2s_s, 0, "low_sclk");
    }

    ret = of_property_read_string(card->dev->of_node, "I2S_LRCLK", &str);
    if (ret < 0) {
        printk("I2S_LRCLK: faild to get gpio I2S_LRCLK!\n");
    }else{
        p_aml_audio->gpio_i2s_r = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_aml_audio->gpio_i2s_r,GPIOF_OUT_INIT_LOW,"low_lrclk");
        amlogic_set_value(p_aml_audio->gpio_i2s_r, 0, "low_lrclk");
    }

    ret = of_property_read_string(card->dev->of_node, "I2S_ODAT", &str);
    if (ret < 0) {
        printk("I2S_ODAT: faild to get gpio I2S_ODAT!\n");
    }else{
        p_aml_audio->gpio_i2s_o = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_aml_audio->gpio_i2s_o,GPIOF_OUT_INIT_LOW,"low_odata");
        amlogic_set_value(p_aml_audio->gpio_i2s_o, 0, "low_odata");
    }
    return 0;
}
static int aml_suspend_post(struct snd_soc_card *card)
{
    printk(KERN_INFO "enter %s\n", __func__);   
    i2s_gpio_set(card);
    return 0;
}

static int aml_resume_pre(struct snd_soc_card *card)
{
    printk(KERN_INFO "enter %s\n", __func__);
    struct aml_audio_private_data *p_aml_audio;
    p_aml_audio = snd_soc_card_get_drvdata(card);  

    if(p_aml_audio->gpio_i2s_m)
        amlogic_gpio_free(p_aml_audio->gpio_i2s_m,"low_mclk");
    if(p_aml_audio->gpio_i2s_s)
        amlogic_gpio_free(p_aml_audio->gpio_i2s_s,"low_sclk");
    if(p_aml_audio->gpio_i2s_r)
        amlogic_gpio_free(p_aml_audio->gpio_i2s_r,"low_lrclk");
    if(p_aml_audio->gpio_i2s_o)
        amlogic_gpio_free(p_aml_audio->gpio_i2s_o,"low_odata");
   

    p_aml_audio->pin_ctl = devm_pinctrl_get_select(card->dev, "aml_snd_m8");
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

static const struct snd_kcontrol_new aml_m8_controls[] = {

    SOC_SINGLE_BOOL_EXT("Amp Spk enable", 0,
        aml_m8_get_spk,
        aml_m8_set_spk),
};

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




static int aml_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
    struct snd_soc_card *card = rtd->card;
    struct snd_soc_codec *codec = rtd->codec;
    struct snd_soc_dapm_context *dapm = &codec->dapm;
    struct aml_audio_private_data * p_aml_audio;
    int ret = 0;
    int hp_paraments[5];
    
    printk(KERN_DEBUG "enter %s \n", __func__);
    p_aml_audio = snd_soc_card_get_drvdata(card);
    
    ret = snd_soc_add_card_controls(codec->card, aml_m8_controls,
                ARRAY_SIZE(aml_m8_controls));
    if (ret)
       return ret;

    /* Add specific widgets */
    snd_soc_dapm_new_controls(dapm, aml_asoc_dapm_widgets,
                  ARRAY_SIZE(aml_asoc_dapm_widgets));
    if (ret)
        return ret;
    
#if HP_DET

    p_aml_audio->sdev.name = "h2w";//for report headphone to android
    ret = switch_dev_register(&p_aml_audio->sdev);
    if (ret < 0){
        printk(KERN_ERR "ASoC: register hp switch dev failed\n");
        return ret;
    }

    p_aml_audio->mic_sdev.name = "mic_dev";//for micphone detect
    ret = switch_dev_register(&p_aml_audio->mic_sdev);
    if (ret < 0){
        printk(KERN_ERR "ASoC: register mic switch dev failed\n");
        return ret;
    }

    ret = snd_soc_jack_new(codec, "hp switch", SND_JACK_HEADPHONE, &p_aml_audio->jack);
    if (ret < 0) {
        printk(KERN_WARNING "Failed to alloc resource for hp switch\n");
    } else {
        ret = snd_soc_jack_add_pins(&p_aml_audio->jack, ARRAY_SIZE(jack_pins), jack_pins);
        if (ret < 0) {
            printk(KERN_WARNING "Failed to setup hp pins\n");
        }
    }

    p_aml_audio->mic_det = of_property_read_bool(card->dev->of_node,"mic_det");

    printk("entern %s : mic_det=%d \n",__func__,p_aml_audio->mic_det);
    ret = of_property_read_u32_array(card->dev->of_node, "hp_paraments", &hp_paraments[0], 5);
    if(ret){
        printk("falied to get hp detect paraments from dts file\n");
    }else{
        p_aml_audio->hp_val_h  = hp_paraments[0];  // hp adc value higher base, hp unplugged
        p_aml_audio->hp_val_l  = hp_paraments[1];  // hp adc value low base, 3 section hp plugged.
        p_aml_audio->mic_val   = hp_paraments[2];  // hp adc value mic detect value.
        p_aml_audio->hp_detal  = hp_paraments[3];  // hp adc value test toerance
        p_aml_audio->hp_adc_ch = hp_paraments[4];  // get adc value from which adc port for hp detect

        printk("hp detect paraments: h=%d,l=%d,mic=%d,det=%d,ch=%d \n",p_aml_audio->hp_val_h,p_aml_audio->hp_val_l,
            p_aml_audio->mic_val,p_aml_audio->hp_detal,p_aml_audio->hp_adc_ch);
    }
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
        .name = "SND_M8_RT5616",
        .stream_name = "AML PCM",
        .cpu_dai_name = "aml-i2s-dai.0",
        .init = aml_asoc_init,
        .platform_name = "aml-i2s.0",
        .codec_name = "rt5616.4-001b",
        .ops = &aml_asoc_ops,
    }, 
    #ifdef CONFIG_SND_SOC_PCM2BT
    {
        .name = "BT Voice",
        .stream_name = "Voice PCM",
        .cpu_dai_name = "aml-pcm-dai.0",
        .codec_dai_name = "pcm2bt-pcm",
        .platform_name = "aml-pcm.0",
        .codec_name = "pcm2bt.0",
        //.ops = &voice_soc_ops,
    },
#endif

    {
        .name = "AML-SPDIF",
        .stream_name = "SPDIF PCM",
        .cpu_dai_name = "aml-spdif-dai.0",
        .codec_dai_name = "dit-hifi",
        .init = NULL,
        .platform_name = "aml-i2s.0",
        .codec_name = "spdif-dit.0",
        .ops = NULL,      
    }, 

};

static struct snd_soc_card aml_snd_soc_card = {
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

static void aml_m8_pinmux_init(struct snd_soc_card *card)
{
    struct aml_audio_private_data *p_aml_audio;
    const char *str=NULL;
    int ret;
    p_aml_audio = snd_soc_card_get_drvdata(card);   
    p_aml_audio->pin_ctl = devm_pinctrl_get_select(card->dev, "aml_snd_m8");
    
    
        
#if USE_EXTERNAL_DAC
#ifndef CONFIG_MESON_TRUSTZONE
    //aml_write_reg32(P_AO_SECURE_REG1,0x00000000);
    aml_clr_reg32_mask(P_AO_SECURE_REG1, ((1<<8) | (1<<1)));
#else
    /* Secure reg can only be accessed in Secure World if TrustZone enabled. */
    //meson_secure_reg_write(P_AO_SECURE_REG1, 0x00000000);
	meson_secure_reg_write(P_AO_SECURE_REG1, meson_secure_reg_read(P_AO_SECURE_REG1) & (~((1<<8) | (1<<1))));
#endif /* CONFIG_MESON_TRUSTZONE */
#endif
    ret = of_property_read_string(card->dev->of_node, "mute_gpio", &str);
    if (ret < 0) {
        printk("aml_snd_m8: faild to get mute_gpio!\n");
    }else{
        p_aml_audio->gpio_mute = amlogic_gpio_name_map_num(str);
        p_aml_audio->mute_inv = of_property_read_bool(card->dev->of_node,"mute_inv");
        amlogic_gpio_request_one(p_aml_audio->gpio_mute,GPIOF_OUT_INIT_HIGH,"mute_spk");
        amlogic_set_value(p_aml_audio->gpio_mute, 0, "mute_spk");
      
        spk_gpio_mute = p_aml_audio->gpio_mute;
        printk(KERN_INFO"pinmux set : spk_gpio_mute=%d\n",spk_gpio_mute);
    }

    printk("=%s==,aml_m8_pinmux_init done,---%d\n",__func__,p_aml_audio->det_pol_inv);
}

static void aml_m8_pinmux_deinit(struct snd_soc_card *card)
{
    struct aml_audio_private_data *p_aml_audio;

    p_aml_audio = snd_soc_card_get_drvdata(card);
    if(p_aml_audio->gpio_hp_det)
        amlogic_gpio_free(p_aml_audio->gpio_hp_det,"hp_det");
    if(p_aml_audio->gpio_mute)
        amlogic_gpio_free(p_aml_audio->gpio_mute,"mute_spk"); 
    if(p_aml_audio->pin_ctl)
        devm_pinctrl_put(p_aml_audio->pin_ctl);
}
static int aml_m8_audio_probe(struct platform_device *pdev)
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

//  aml_codec_dai_link[0].codec_of_node = of_parse_phandle(
//          pdev->dev.of_node, "aml,audio-codec", 0);

    ret = snd_soc_register_card(card);
    if (ret) {
        dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
            ret);
        goto err;
    }

    aml_m8_pinmux_init(card);


    return 0;
#endif

err:
    kfree(p_aml_audio);
    return ret;
}

static int aml_m8_audio_remove(struct platform_device *pdev)
{
    int ret = 0;
    struct snd_soc_card *card = platform_get_drvdata(pdev);
    struct aml_audio_private_data *p_aml_audio;

    p_aml_audio = snd_soc_card_get_drvdata(card);
    snd_soc_unregister_card(card);
#if HP_DET
    /* stop timer */
    mutex_lock(&p_aml_audio->lock);
    if (p_aml_audio->timer_en) {
        aml_audio_stop_timer(p_aml_audio);
    }
    mutex_unlock(&p_aml_audio->lock);
#endif

    aml_m8_pinmux_deinit(card);
    kfree(p_aml_audio);
    return ret;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_audio_dt_match[]={
    { .compatible = "sound_card, aml_m8_rt5616", },
    {},
};
#else
#define amlogic_audio_dt_match NULL
#endif

static struct platform_driver aml_m8_audio_driver = {
    .probe  = aml_m8_audio_probe,
    .remove = aml_m8_audio_remove,
    .driver = {
        .name = DRV_NAME,
        .owner = THIS_MODULE,
        .pm = &snd_soc_pm_ops,
        .of_match_table = amlogic_audio_dt_match,
    },
};

static int __init aml_m8_audio_init(void)
{
    return platform_driver_register(&aml_m8_audio_driver);
}

static void __exit aml_m8_audio_exit(void)
{
    platform_driver_unregister(&aml_m8_audio_driver);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(aml_m8_audio_init);
#else
module_init(aml_m8_audio_init);
#endif
module_exit(aml_m8_audio_exit);

/* Module information */
MODULE_AUTHOR("AMLogic, Inc.");
MODULE_DESCRIPTION("AML_M8 audio machine Asoc driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, amlogic_audio_dt_match);

