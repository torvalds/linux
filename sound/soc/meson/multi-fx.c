/*
 * multi-fx.c  --  SoC audio for Multi-FX pedal audio device
 *
 * Copyright (c) 2020 Rezzonics <rezzonics@gmail.com>
 *
 * based on code from:
 *
 *    Wolfson Microelectronics PLC.@
 *    Openedhand Ltd.
 *    Liam Girdwood <lrg@slimlogic.co.uk>
 *    Richard Purdie <richard@openedhand.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <plat/sys_config.h>
#include <linux/io.h>
#include <sound/tlv.h>

#include "i2s/meson-i2s.h"
#include "i2s/meson-i2sdma.h"
#include "../codecs/cs4245.h"

// GPIO macros
#define PIN_DIR_OUT     1
#define CHANNEL_A       0
#define CHANNEL_B       1

// Input Impedance
#define LINE            0	//  1 MOhm
#define INSTRUMENT      1	//  3 KOhm
#define MICROPHONE      2	// 75 kOhm

// Input Analog Gain
#define GAIN_STAGE_24_3_DB   0 // 24.3 dB
#define GAIN_STAGE_11_8_DB   1 // 11.8 dB
#define GAIN_STAGE_0_7_DB    2 // -0.7 dB
#define GAIN_STAGE_OFF       3 // -1 dB

static unsigned int gain_stage_left_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 0, TLV_DB_SCALE_ITEM(24.3,  0, 0),
	1, 1, TLV_DB_SCALE_ITEM(11.8, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-0.7, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(-1, 0, 0),
};

static unsigned int gain_stage_right_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 0, TLV_DB_SCALE_ITEM(24.3,  0, 0),
	1, 1, TLV_DB_SCALE_ITEM(11.8, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-0.7, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(-1, 0, 0),
};

#define BYPASS          0
#define PROCESS         1

#define TURN_SWITCH_ON	0
#define TURN_SWITCH_OFF 1

#define HP_TURN_SWITCH_ON  1
#define HP_TURN_SWITCH_OFF 0

//Default headphone volume is 11th step (of a total of 16) which corresponds to a 0dB gain.
//Each step corresponds to 3dB.
static int headphone_volume = 11;

static int input_left_gain_stage = 0;
static int input_right_gain_stage = 0;
static int left_true_bypass = 0;
static int right_true_bypass = 0;

static int multi_fx_used = 0;
static u32 multi_fx_gpio_handler = 0;

struct clk *codec_pll2clk,*codec_moduleclk;

#define MULTI_FX_GPIO_INIT(name)\
    err = script_parser_fetch("multi_fx_soundcard_para", name, (int *) &info, sizeof (script_gpio_set_t));\
    if (err) {\
        printk(KERN_INFO "%s: can not get \"multi_fx_soundcard_para\" \"" name "\" gpio handler, already used by others?.", __FUNCTION__);\
        return -EBUSY;\
    }\
    gpio_set_one_pin_io_status(multi_fx_gpio_handler, PIN_DIR_OUT, name);

/*
* GPIO Initialization
*/
static int multi_fx_gpio_init(void)
{
    int err;
    script_gpio_set_t info;

    printk("[Multi-FX Machine Driver] %s\n", __func__);

    multi_fx_gpio_handler = gpio_request_ex("multi_fx_soundcard_para", NULL);

    // Gain Control Switches Pin Configuration
    MULTI_FX_GPIO_INIT("left_gain_ctrl1")
    MULTI_FX_GPIO_INIT("left_gain_ctrl2")
    MULTI_FX_GPIO_INIT("right_gain_ctrl1")
    MULTI_FX_GPIO_INIT("right_gain_ctrl2")

    // Headphone Volume Control Pin Configuration
    // TODO: Create a separate driver for Headphone Amplifier (LM4811).
    MULTI_FX_GPIO_INIT("headphone_ctrl")
    MULTI_FX_GPIO_INIT("headphone_clk")

    // True Bypass Control Pin Configuration
    MULTI_FX_GPIO_INIT("true_bypass_left")
    MULTI_FX_GPIO_INIT("true_bypass_right")

    // Initial GPIO values
    gpio_write_one_pin_value(multi_fx_gpio_handler, TURN_SWITCH_OFF, "left_gain_ctrl1");
    gpio_write_one_pin_value(multi_fx_gpio_handler, TURN_SWITCH_OFF, "left_gain_ctrl2");
    gpio_write_one_pin_value(multi_fx_gpio_handler, TURN_SWITCH_OFF, "right_gain_ctrl1");
    gpio_write_one_pin_value(multi_fx_gpio_handler, TURN_SWITCH_OFF, "right_gain_ctrl2");
    gpio_write_one_pin_value(multi_fx_gpio_handler, 0, "headphone_clk");
    gpio_write_one_pin_value(multi_fx_gpio_handler, 0, "true_bypass_left");
    gpio_write_one_pin_value(multi_fx_gpio_handler, 0, "true_bypass_right");

    //Make sure we enable internall pullup's
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "true_bypass_left");
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "true_bypass_right");
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "headphone_clk");
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "headphone_ctrl");
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "left_gain_ctrl1");
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "left_gain_ctrl2");
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "right_gain_ctrl1");
    gpio_set_one_pin_pull(multi_fx_gpio_handler, 1, "right_gain_ctrl2");

    printk("[Multi-FX Machine Driver] GPIOs initialized.\n");
    return 0;
}

static void multi_fx_gpio_release(void)
{
    gpio_release(multi_fx_gpio_handler, 2);
    printk("[Multi-FX Machine Driver] GPIOs released.\n");
    return;
}

static void multi_fx_set_gain_stage(int channel, int state)
{
    switch(channel){
        case CHANNEL_A:
            gpio_write_one_pin_value(multi_fx_gpio_handler, (state & 2) ? TURN_SWITCH_ON : TURN_SWITCH_OFF, "left_gain_ctrl1");
            gpio_write_one_pin_value(multi_fx_gpio_handler, (state & 1) ? TURN_SWITCH_ON : TURN_SWITCH_OFF, "left_gain_ctrl2");
            input_left_gain_stage = state;
            break;
        case CHANNEL_B:
            gpio_write_one_pin_value(multi_fx_gpio_handler, (state & 2) ? TURN_SWITCH_ON : TURN_SWITCH_OFF, "right_gain_ctrl1");
            gpio_write_one_pin_value(multi_fx_gpio_handler, (state & 1) ? TURN_SWITCH_ON : TURN_SWITCH_OFF, "right_gain_ctrl2");
            input_right_gain_stage = state;
            break;
    }
    return;
}

static void multi_fx_set_true_bypass(int channel, bool state)
{
    // state == BYPASS:
    //   No audio processing.
    //   Input is connected directly to output, bypassing the codec.
    //
    // state == PROCESS:
    //   INPUT => CODEC => OUTPUT

    switch(channel)
    {
        case CHANNEL_A:
            if (state == BYPASS)
            {
                gpio_write_one_pin_value(multi_fx_gpio_handler, 1, "true_bypass_left");
            }
            else if (state == PROCESS)
            {
                gpio_write_one_pin_value(multi_fx_gpio_handler, 0, "true_bypass_left");
            }
            left_true_bypass = state;
            break;
        case CHANNEL_B:
            if (state == BYPASS)
            {
                gpio_write_one_pin_value(multi_fx_gpio_handler, 1, "true_bypass_right");
            }
            else if (state == PROCESS)
            {
                gpio_write_one_pin_value(multi_fx_gpio_handler, 0, "true_bypass_right");
            }
            right_true_bypass = state;
            break;
    }
}

/* 
* TODO: Check if the _set_fmt and _setsysclk of cpu_dai and codec_dai should really be called here. 
* TODO: I think the functions should be called on multi_fx_audio_init. 
* TODO: This function is beeing called by aplay every time.
* TODO: I think set_fmt and set_sysclk should be called once only on the initialization of the driver.
* TODO: Compare with other machine drivers startup functions. Seems like it is used to update the DAPM external controllers (ALSA Mixer).
* TODO: Could be on snd_soc_dai_link.init? Just asked on #alsa-soc.
* TODO: Following #alsa-soc, try late_probe() (snd_soc_card) or init() (snd_soc_dai_link).
*/
static int multi_fx_startup(struct snd_pcm_substream *substream)
{
    // int ret;
    // struct snd_soc_pcm_runtime *rtd = substream->private_data;
    // struct snd_soc_dai *codec_dai = rtd->codec_dai;
    // struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    // unsigned int fmt = 0;
    // unsigned int mclk = 24576000;	// Multi-FX Sound Card has an 2457600Hz external clock

    printk("[Multi-FX Machine Driver] %s\n", __func__);

    // // Initialize the CS4245 Codec Driver
    // fmt = 	SND_SOC_DAIFMT_I2S |	/* I2S mode */
    //    	 	SND_SOC_DAIFMT_CBM_CFS;	// CS4245: DAC master ADC slave (ALSA: codec clk master & frame slave).

    // //call cs4245_set_dai_fmt (CS4245 Codec Driver) - CODEC DAI.
    // ret = snd_soc_dai_set_fmt(codec_dai, fmt);	// Calls snd_soc_dai_driver .ops->set_fmt
    // if (ret < 0)
    // 	return ret;

    // //call cs4245_set_dai_sysclk (CS4245 Codec Driver) - CODEC DAI.
    // ret = snd_soc_dai_set_sysclk(codec_dai, CS4245_MCLK1_SET , mclk, 0);
    // if (ret < 0)
    // 	return ret;

    // // Initialize the I2S Plataform Driver
    // fmt = 	SND_SOC_DAIFMT_CBS_CFS |					// SoC clk & frm slave.
    // 		SND_SOC_DAIFMT_I2S |						// I2S mode
    // 		SND_SOC_DAIFMT_MESON_IISFAT0_WSS_32BCLK |	// Word Size = 32.
    // 		SND_SOC_DAIFMT_NB_NF;						// normal bit clock + frame.

    // //call meson_i2s_set_fmt (I2S Plataform Driver)- CPU DAI.
    // ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
    // if (ret < 0)
    // 	return ret;

    return 0;
}

static void multi_fx_shutdown(struct snd_pcm_substream *substream)
{
    printk("[Multi-FX Machine Driver] %s\n", __func__);
    return;
}

static int multi_fx_analog_suspend(struct snd_soc_card *card)
{
    printk("[Multi-FX Machine Driver] %s\n", __func__);
    return 0;
}

static int multi_fx_analog_resume(struct snd_soc_card *card)
{
    printk("[Multi-FX Machine Driver] %s\n", __func__);
    return 0;
}

/* This routine flips the GPIO pins to send the volume adjustment
   message to the actual headphone gain-control chip (LM4811) */
static void set_headphone_volume(int new_volume){
    int steps = new_volume - headphone_volume;
    int i;

    //select volume adjustment direction:
    gpio_write_one_pin_value(multi_fx_gpio_handler, steps > 0 ? HP_TURN_SWITCH_ON : HP_TURN_SWITCH_OFF, "headphone_ctrl");

    for (i=0; i < abs(steps); i++) {
        //toggle clock in order to sample the volume pin upon clock's rising edge:
        gpio_write_one_pin_value(multi_fx_gpio_handler, HP_TURN_SWITCH_OFF, "headphone_clk");
        gpio_write_one_pin_value(multi_fx_gpio_handler, HP_TURN_SWITCH_ON, "headphone_clk");
    }

    headphone_volume = new_volume;
}

static int headphone_info(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 15;
    return 0;
}

static int headphone_get(struct snd_kcontrol *kcontrol,
                         struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = headphone_volume;
    return 0;
}

static int headphone_put(struct snd_kcontrol *kcontrol,
                         struct snd_ctl_elem_value *ucontrol)
{
    if (headphone_volume == ucontrol->value.integer.value[0])
        return 0;

    set_headphone_volume(ucontrol->value.integer.value[0]);
    return 1;
}

static struct snd_kcontrol_new headphone_control __devinitdata = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .name = "Headphone Playback Volume",
    .index = 0,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .info = headphone_info,
    .get = headphone_get,
    .put = headphone_put
};

//----------------------------------------------------------------------

static int input_left_gain_stage_info(struct snd_kcontrol *kcontrol,
                                      struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 3;
    return 0;
}

static int input_left_gain_stage_get(struct snd_kcontrol *kcontrol,
                                     struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = input_left_gain_stage;
    return 0;
}

static int input_left_gain_stage_put(struct snd_kcontrol *kcontrol,
                                     struct snd_ctl_elem_value *ucontrol)
{
    if (input_left_gain_stage == ucontrol->value.integer.value[0])
        return 0;

    multi_fx_set_gain_stage(CHANNEL_A, ucontrol->value.integer.value[0]);
    return 1;
}

//----------------------------------------------------------------------

static int input_right_gain_stage_info(struct snd_kcontrol *kcontrol,
                                       struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 3;
    return 0;
}

static int input_right_gain_stage_get(struct snd_kcontrol *kcontrol,
                                      struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = input_right_gain_stage;
    return 0;
}

static int input_right_gain_stage_put(struct snd_kcontrol *kcontrol,
                                      struct snd_ctl_elem_value *ucontrol)
{
    if (input_right_gain_stage == ucontrol->value.integer.value[0])
        return 0;

    multi_fx_set_gain_stage(CHANNEL_B, ucontrol->value.integer.value[0]);
    return 1;
}

//----------------------------------------------------------------------

static int left_true_bypass_info(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;
    return 0;
}

static int left_true_bypass_get(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = left_true_bypass;
    return 0;
}

static int left_true_bypass_put(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
    if (left_true_bypass == ucontrol->value.integer.value[0])
        return 0;

    multi_fx_set_true_bypass(CHANNEL_A, ucontrol->value.integer.value[0]);
    return 1;
}

//----------------------------------------------------------------------

static int right_true_bypass_info(struct snd_kcontrol *kcontrol,
                                  struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;
    return 0;
}

static int right_true_bypass_get(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
    ucontrol->value.integer.value[0] = right_true_bypass;
    return 0;
}


static int right_true_bypass_put(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
    if (right_true_bypass == ucontrol->value.integer.value[0])
        return 0;

    multi_fx_set_true_bypass(CHANNEL_B, ucontrol->value.integer.value[0]);
    return 1;
}

//----------------------------------------------------------------------

static struct snd_kcontrol_new input_left_gain_stage_control __devinitdata = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .name = "Left Gain Stage",
    .index = 0,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .info = input_left_gain_stage_info,
    .get = input_left_gain_stage_get,
    .put = input_left_gain_stage_put,
    .tlv.p = gain_stage_left_tlv
};

static struct snd_kcontrol_new input_right_gain_stage_control __devinitdata = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .name = "Right Gain Stage",
    .index = 0,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .info = input_right_gain_stage_info,
    .get = input_right_gain_stage_get,
    .put = input_right_gain_stage_put,
    .tlv.p = gain_stage_right_tlv
};

static struct snd_kcontrol_new left_true_bypass_control __devinitdata = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .name = "Left True-Bypass",
    .index = 0,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .info = left_true_bypass_info,
    .get = left_true_bypass_get,
    .put = left_true_bypass_put
};

static struct snd_kcontrol_new right_true_bypass_control __devinitdata = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .name = "Right True-Bypass",
    .index = 0,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .info = right_true_bypass_info,
    .get = right_true_bypass_get,
    .put = right_true_bypass_put
};

static int snd_soc_multi_fx_probe(struct snd_soc_card *card)
{
    struct snd_card* snd_card = card->snd_card;
    int ret;

    if (!snd_card){
        printk("[Multi-FX Machine Driver] Error trying to register ALSA Controls\n");
        return 0;
    }

    ret = snd_ctl_add(snd_card, snd_ctl_new1(&headphone_control, NULL));
    if (ret < 0)
        return ret;

    ret = snd_ctl_add(snd_card, snd_ctl_new1(&input_left_gain_stage_control, NULL));
    if (ret < 0)
        return ret;

    ret = snd_ctl_add(snd_card, snd_ctl_new1(&input_right_gain_stage_control, NULL));
    if (ret < 0)
        return ret;

    ret = snd_ctl_add(snd_card, snd_ctl_new1(&left_true_bypass_control, NULL));
    if (ret < 0)
        return ret;

    ret = snd_ctl_add(snd_card, snd_ctl_new1(&right_true_bypass_control, NULL));
    if (ret < 0)
        return ret;

    return 0;
}

//----------------------------------------------------------------------

static int multi_fx_hw_params(struct snd_pcm_substream *substream,
                             struct snd_pcm_hw_params *params)
{
#if 0
    /* debug */
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

    printk("[Multi-FX Machine Driver]multi_fx_hw_params\n");

    printk("[Multi-FX Machine Driver]multi_fx_hw_params: codec_dai=(%s), cpu_dai=(%s).\n", codec_dai->name, cpu_dai->name);
    printk("[Multi-FX Machine Driver]multi_fx_hw_params: channel num=(%d).\n", params_channels(params));
    printk("[Multi-FX Machine Driver]multi_fx_hw_params: sample rate=(%u).\n", params_rate(params));

    switch (params_format(params)){
        case SNDRV_PCM_FORMAT_S16_LE:
            printk("[Multi-FX Machine Driver]multi_fx_hw_params: format 16 bit.\n");
            break;
        case SNDRV_PCM_FORMAT_S24_3LE:
            printk("[Multi-FX Machine Driver]multi_fx_hw_params: format 24 bit in 3 bytes.\n");
            break;
        case SNDRV_PCM_FORMAT_S24_LE:
            printk("[Multi-FX Machine Driver]multi_fx_hw_params: format 24 bit in 4 bytes.\n");
            break;
        default:
            printk("[Multi-FX Machine Driver]multi_fx_hw_params: Unsupported format (%d).\n", (int)params_format(params));
    }
#endif

    return 0;
}

/*
* Implemented to replace the calls made in multi_fx_startup.
* The multi_fx_startup and then cpu_dai and codec_dai *_set_fmt and *_set_sysclk were called every aplay, and the reconfiguration was redundant.
*/
int multi_fx_dai_link_init(struct snd_soc_pcm_runtime *rtd)
{
    int ret;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    unsigned int fmt = 0;
    const unsigned int mclk = 24576000;
    /* Multi-FX Sound Card does not have an 2457600Hz external clock
       so we have to confirue the system so that it generates this
       clock sinal for feeding the Codec MCLK1 pin*/

    printk("[Multi-FX Machine Driver] %s\n", __func__);

    // Configure the CS4245 Codec Driver for Multi-FX Sound Card
    fmt = SND_SOC_DAIFMT_I2S |      /* I2S mode */
          SND_SOC_DAIFMT_CBM_CFS;   /* CS4245: DAC master ADC slave
                                       (ALSA: codec clk master & frame slave). */

    //call cs4245_set_dai_fmt (CS4245 Codec Driver) - CODEC DAI.
    ret = snd_soc_dai_set_fmt(codec_dai, fmt);	// Calls snd_soc_dai_driver .ops->set_fmt
    if (ret < 0)
        return ret;

    //call cs4245_set_dai_sysclk (CS4245 Codec Driver) - CODEC DAI.
    ret = snd_soc_dai_set_sysclk(codec_dai, CS4245_MCLK1_SET, mclk, 0);
    if (ret < 0)
        return ret;

    /* Setup I2S-related clock signals */
    codec_pll2clk = clk_get(NULL,"audio_pll");
    ret = clk_enable(codec_pll2clk);
    if (ret < 0){
        printk("[Multi-FX Machine Driver] clk_enable(codec_pll2clk) failed; \n");
        return ret;
    }

    codec_moduleclk = clk_get(NULL,"audio_codec");
    ret = clk_set_parent(codec_moduleclk, codec_pll2clk);
    if (ret < 0) {
        printk("[Multi-FX Machine Driver] try to set parent of codec_moduleclk to codec_pll2clk failed!\n");
        return ret;
    }

    ret = clk_enable(codec_moduleclk);
    if (ret < 0){
        printk("[Multi-FX Machine Driver] clk_enable(codec_moduleclk) failed; \n");
        return ret;
    }

    // Configure the I2S Plataform Driver for Multi-FX Sound Card
    fmt = SND_SOC_DAIFMT_CBS_CFS |                  // SoC clk & frm slave.
          SND_SOC_DAIFMT_I2S |                      // I2S mode
          SND_SOC_DAIFMT_MESON_IISFAT0_WSS_32BCLK | // Word Size = 32.
          SND_SOC_DAIFMT_NB_NF;                     // normal bit clock + frame.

    //call meson_i2s_set_fmt (I2S Plataform Driver)- CPU DAI.
    ret = snd_soc_dai_set_fmt(cpu_dai, fmt);

    return ret;
}

static struct snd_soc_ops multi_fx_ops = {
    .startup = multi_fx_startup,
    .shutdown = multi_fx_shutdown,
    .hw_params = multi_fx_hw_params,
};

static struct snd_soc_dai_link multi_fx_dai =
{
    .name = "MULTI-FXO-I2S",
    .stream_name = "MULTI-FXO-MESON-I2S",
    .cpu_dai_name = "meson-i2s.0",
    .codec_dai_name = "cs4245-dai",
    .platform_name = "meson-i2s-pcm-audio.0",
    .ops = &multi_fx_ops,
    .init = &multi_fx_dai_link_init,
};

static struct snd_soc_card snd_soc_multi_fx_soundcard = {
    .name = "MULTI-FXO",
    .owner = THIS_MODULE,
    .dai_link = &multi_fx_dai,
    .num_links = 1,
    .suspend_post = multi_fx_analog_suspend,
    .resume_pre	= multi_fx_analog_resume,
    .probe = snd_soc_multi_fx_probe,
};

static int __devexit multi_fx_audio_remove(struct platform_device *pdev)
{
    struct snd_soc_card *card = platform_get_drvdata(pdev);

    if (multi_fx_used) {
        multi_fx_gpio_release();
    }

    snd_soc_unregister_card(card);
    return 0;
}

static int __devinit multi_fx_audio_probe(struct platform_device *pdev)
{
    struct snd_soc_card* card = &snd_soc_multi_fx_soundcard;
    int ret, i2s_used, codec_twi_id;
    static char codec_name[32];

    printk("[Multi-FX Machine Driver] %s\n", __func__);

    ret = script_parser_fetch("i2s_para", "i2s_used", &i2s_used, 1);
    if ((ret != 0) || (!i2s_used)) {
        printk("[Multi-FX Machine Driver]I2S not configured on script.bin.\n");
        return -ENODEV;
    }

    ret = script_parser_fetch("codec_para", "codec_twi_id", &codec_twi_id, 1);
    if ((ret != 0) || (!i2s_used)) {
        printk("[Multi-FX Machine Driver]CODEC twi id not configured on script.bin.\n");
        return -ENODEV;
    }

    snprintf(codec_name, sizeof(codec_name), "cs4245-codec.%i-004c", codec_twi_id);
    card->dai_link->codec_name = codec_name;

    ret = script_parser_fetch("multi_fx_soundcard_para","multi_fx_soundcard_used", &multi_fx_used, sizeof(int));
    if ((ret != 0) || (!multi_fx_used)) {
        printk("[Multi-FX Machine Driver]Multi-FX Sound Card not configured on script.bin.\n");
        return -ENODEV;
    }

    card->dev = &pdev->dev;

    ret = snd_soc_register_card(card);
    if (ret) {
        dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",	ret);
        return ret;
    }

    if (multi_fx_used) {
        multi_fx_gpio_init();
        multi_fx_set_gain_stage(CHANNEL_A, GAIN_STAGE_OFF);
        multi_fx_set_gain_stage(CHANNEL_B, GAIN_STAGE_OFF);
        multi_fx_set_true_bypass(CHANNEL_A, PROCESS);
        multi_fx_set_true_bypass(CHANNEL_B, PROCESS);
    }

    return ret;
}

static struct platform_driver multi_fx_audio_driver = {
    .driver		= {
        .name	= "multi-fx-audio",
        .owner	= THIS_MODULE,
    },
    .probe		= multi_fx_audio_probe,
    .remove		= __devexit_p(multi_fx_audio_remove),
};

module_platform_driver(multi_fx_audio_driver);

/* Module information */
MODULE_AUTHOR("Rezzonics <rezzonics@gmail.com>);
MODULE_DESCRIPTION("Multi-FX Sound Card Audio Machine Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:multi-fx-audio");
