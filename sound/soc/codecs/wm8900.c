/*
 * wm8900.c  --  WM8900 ALSA Soc Audio driver
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO:
 *  - Tristating.
 *  - TDM.
 *  - Jack detect.
 *  - FLL source configuration, currently only MCLK is supported.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>

#include "wm8900.h"


#if 0
#define	WM8900_DBG(x...)	printk(KERN_INFO x)
#else
#define	WM8900_DBG(x...)
#endif

/* WM8900 register space */
#define WM8900_REG_RESET	0x0
#define WM8900_REG_ID		0x0
#define WM8900_REG_POWER1	0x1
#define WM8900_REG_POWER2	0x2
#define WM8900_REG_POWER3	0x3
#define WM8900_REG_AUDIO1	0x4
#define WM8900_REG_AUDIO2	0x5
#define WM8900_REG_CLOCKING1    0x6
#define WM8900_REG_CLOCKING2    0x7
#define WM8900_REG_AUDIO3       0x8
#define WM8900_REG_AUDIO4       0x9
#define WM8900_REG_DACCTRL      0xa
#define WM8900_REG_LDAC_DV      0xb
#define WM8900_REG_RDAC_DV      0xc
#define WM8900_REG_SIDETONE     0xd
#define WM8900_REG_ADCCTRL      0xe
#define WM8900_REG_LADC_DV	0xf
#define WM8900_REG_RADC_DV      0x10
#define WM8900_REG_GPIO         0x12
#define WM8900_REG_INCTL	0x15
#define WM8900_REG_LINVOL	0x16
#define WM8900_REG_RINVOL	0x17
#define WM8900_REG_INBOOSTMIX1  0x18
#define WM8900_REG_INBOOSTMIX2  0x19
#define WM8900_REG_ADCPATH	0x1a
#define WM8900_REG_AUXBOOST	0x1b
#define WM8900_REG_ADDCTL       0x1e
#define WM8900_REG_FLLCTL1      0x24
#define WM8900_REG_FLLCTL2      0x25
#define WM8900_REG_FLLCTL3      0x26
#define WM8900_REG_FLLCTL4      0x27
#define WM8900_REG_FLLCTL5      0x28
#define WM8900_REG_FLLCTL6      0x29
#define WM8900_REG_LOUTMIXCTL1  0x2c
#define WM8900_REG_ROUTMIXCTL1  0x2d
#define WM8900_REG_BYPASS1	0x2e
#define WM8900_REG_BYPASS2	0x2f
#define WM8900_REG_AUXOUT_CTL   0x30
#define WM8900_REG_LOUT1CTL     0x33
#define WM8900_REG_ROUT1CTL     0x34
#define WM8900_REG_LOUT2CTL	0x35
#define WM8900_REG_ROUT2CTL	0x36
#define WM8900_REG_HPCTL1	0x3a
#define WM8900_REG_OUTBIASCTL   0x73

#define WM8900_MAXREG		0x80

#define WM8900_REG_ADDCTL_OUT1_DIS    0x80
#define WM8900_REG_ADDCTL_OUT2_DIS    0x40
#define WM8900_REG_ADDCTL_VMID_DIS    0x20
#define WM8900_REG_ADDCTL_BIAS_SRC    0x10
#define WM8900_REG_ADDCTL_VMID_SOFTST 0x04
#define WM8900_REG_ADDCTL_TEMP_SD     0x02

#define WM8900_REG_GPIO_TEMP_ENA   0x2

#define WM8900_REG_POWER1_STARTUP_BIAS_ENA 0x0100
#define WM8900_REG_POWER1_BIAS_ENA         0x0008
#define WM8900_REG_POWER1_VMID_BUF_ENA     0x0004
#define WM8900_REG_POWER1_FLL_ENA          0x0040

#define WM8900_REG_POWER2_SYSCLK_ENA  0x8000
#define WM8900_REG_POWER2_ADCL_ENA    0x0002
#define WM8900_REG_POWER2_ADCR_ENA    0x0001

#define WM8900_REG_POWER3_DACL_ENA    0x0002
#define WM8900_REG_POWER3_DACR_ENA    0x0001

#define WM8900_REG_AUDIO1_AIF_FMT_MASK 0x0018
#define WM8900_REG_AUDIO1_LRCLK_INV    0x0080
#define WM8900_REG_AUDIO1_BCLK_INV     0x0100

#define WM8900_REG_CLOCKING1_BCLK_DIR   0x1
#define WM8900_REG_CLOCKING1_MCLK_SRC   0x100
#define WM8900_REG_CLOCKING1_BCLK_MASK  (~0x01e)
#define WM8900_REG_CLOCKING1_OPCLK_MASK (~0x7000)

#define WM8900_REG_CLOCKING2_ADC_CLKDIV 0x1c
#define WM8900_REG_CLOCKING2_DAC_CLKDIV 0xe0

#define WM8900_REG_DACCTRL_MUTE          0x004
#define WM8900_REG_DACCTRL_DAC_SB_FILT   0x100
#define WM8900_REG_DACCTRL_AIF_LRCLKRATE 0x400

#define WM8900_REG_AUDIO3_ADCLRC_DIR    0x0800

#define WM8900_REG_AUDIO4_DACLRC_DIR    0x0800

#define WM8900_REG_FLLCTL1_OSC_ENA    0x100

#define WM8900_REG_FLLCTL6_FLL_SLOW_LOCK_REF 0x100

#define WM8900_REG_HPCTL1_HP_IPSTAGE_ENA 0x80
#define WM8900_REG_HPCTL1_HP_OPSTAGE_ENA 0x40
#define WM8900_REG_HPCTL1_HP_CLAMP_IP    0x20
#define WM8900_REG_HPCTL1_HP_CLAMP_OP    0x10
#define WM8900_REG_HPCTL1_HP_SHORT       0x08
#define WM8900_REG_HPCTL1_HP_SHORT2      0x04

#define WM8900_LRC_MASK 0xfc00
#define SPK_CON 		RK29_PIN6_PB6

#define WM8900_NO_POWEROFF /* Do not close codec except suspend or poweroff */

#define WM8900_IS_SHUTDOWN	0
#define WM8900_IS_STARTUP	1

#define WM8900_WORK_NULL	0
#define WM8900_WORK_POWERDOWN_PLAYBACK	1
#define WM8900_WORK_POWERDOWN_CAPTURE	2
#define WM8900_WORK_POWERDOWN_PLAYBACK_CAPTURE	3
#define WM8900_WORK_HW_SET 4

static void wm8900_work(struct work_struct *work);

static struct workqueue_struct *wm8900_workq;
static DECLARE_DELAYED_WORK(delayed_work, wm8900_work);
static int wm8900_current_status = WM8900_IS_SHUTDOWN, wm8900_work_type = WM8900_WORK_NULL;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static struct snd_soc_codec_driver soc_codec_dev_wm8900;
#endif
static struct snd_soc_codec *wm8900_codec;
static bool isSPKon = true;

struct wm8900_priv {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	enum snd_soc_control_type control_type;
#endif
	struct snd_soc_codec codec;

	u16 reg_cache[WM8900_MAXREG];

	u32 fll_in; /* FLL input frequency */
	u32 fll_out; /* FLL output frequency */
};

/*
 * wm8900 register cache.  We can't read the entire register space and we
 * have slow control buses so we cache the registers.
 */
static const u16 wm8900_reg_defaults[WM8900_MAXREG] = {
	0x8900, 0x0000,
	0xc000, 0x0000,
	0x4050, 0x4000,
	0x0008, 0x0000,
	0x0040, 0x0040,
	0x1004, 0x00c0,
	0x00c0, 0x0000,
	0x0100, 0x00c0,
	0x00c0, 0x0000,
	0xb001, 0x0000,
	0x0000, 0x0044,
	0x004c, 0x004c,
	0x0044, 0x0044,
	0x0000, 0x0044,
	0x0000, 0x0000,
	0x0002, 0x0000,
	0x0000, 0x0000,
	0x0000, 0x0000,
	0x0008, 0x0000,
	0x0000, 0x0008,
	0x0097, 0x0100,
	0x0000, 0x0000,
	0x0050, 0x0050,
	0x0055, 0x0055,
	0x0055, 0x0000,
	0x0000, 0x0079,
	0x0079, 0x0079,
	0x0079, 0x0000,
	/* Remaining registers all zero */
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int wm8900_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
#else
static int wm8900_volatile_register(unsigned int reg)
#endif
{
	switch (reg) {
	case WM8900_REG_ID:
		return 1;
	default:
		return 0;
	}
}

static void wm8900_reset(struct snd_soc_codec *codec)
{
	WM8900_DBG("Enter:%s, %d, codec=%p\n", __FUNCTION__, __LINE__,codec);

	snd_soc_write(codec, WM8900_REG_RESET, 0);

	memcpy(codec->reg_cache, wm8900_reg_defaults,
	       sizeof(wm8900_reg_defaults));
}

void codec_set_spk(bool on)
{
	isSPKon = on;
	if (on) {
#ifdef SPK_CON
		gpio_set_value(SPK_CON, GPIO_HIGH);
#endif
	} else {
#ifdef SPK_CON
		gpio_set_value(SPK_CON, GPIO_LOW);
#endif
	}
}

EXPORT_SYMBOL_GPL(codec_set_spk);

static void wm8900_powerdown(void)
{
	printk("Power down wm8900\n");
#ifndef WM8900_NO_POWEROFF
	gpio_set_value(RK29_PIN1_PD6, GPIO_LOW);
#endif

	snd_soc_write(wm8900_codec, WM8900_REG_POWER1, 0x210D);

	if (wm8900_current_status != WM8900_IS_SHUTDOWN) {
#ifdef SPK_CON
		gpio_set_value(SPK_CON, GPIO_LOW);
#endif
		msleep(20);
		snd_soc_write(wm8900_codec, WM8900_REG_RESET, 0);
		wm8900_current_status = WM8900_IS_SHUTDOWN;
	}
}

static void wm8900_set_hw(struct snd_soc_codec *codec)
{
	u16 reg;

	if (wm8900_current_status & WM8900_IS_STARTUP)
		return;

	printk("Power up wm8900\n");
//CLK , PATH, VOL,POW.
	
	snd_soc_write(codec, WM8900_REG_HPCTL1, 0x30);
	snd_soc_write(codec, WM8900_REG_POWER1, 0x0100);
	snd_soc_write(codec, WM8900_REG_POWER3, 0x60);
	snd_soc_write(codec, WM8900_REG_POWER1, 0x0101);
	msleep(400);
	snd_soc_write(codec, WM8900_REG_POWER1, 0x0109);
	snd_soc_write(codec, WM8900_REG_ADDCTL, 0x02);
	snd_soc_write(codec, WM8900_REG_POWER1, 0x09);
	snd_soc_write(codec, WM8900_REG_POWER3, 0xEF);
	snd_soc_write(codec, WM8900_REG_DACCTRL, WM8900_REG_DACCTRL_MUTE);
	snd_soc_write(codec, WM8900_REG_LOUTMIXCTL1, 0x150);
	snd_soc_write(codec, WM8900_REG_ROUTMIXCTL1, 0x150);

	snd_soc_write(codec, WM8900_REG_HPCTL1, 0xB0);
	snd_soc_write(codec, WM8900_REG_HPCTL1, 0xF0);
	snd_soc_write(codec, WM8900_REG_HPCTL1, 0xC0);

	//for recorder
	snd_soc_write(codec, WM8900_REG_POWER1, 0x210D);
	snd_soc_write(codec, WM8900_REG_POWER2, 0xC1AF);

	snd_soc_write(codec, WM8900_REG_LADC_DV, 0x01C0);
	snd_soc_write(codec, WM8900_REG_RADC_DV, 0x01C0);

	snd_soc_write(codec, WM8900_REG_INCTL, 0x0040);

	snd_soc_write(codec, WM8900_REG_LINVOL, 0x011A);
	snd_soc_write(codec, WM8900_REG_RINVOL, 0x011A);
	snd_soc_write(codec, WM8900_REG_INBOOSTMIX1, 0x0042);
	snd_soc_write(codec, WM8900_REG_INBOOSTMIX2, 0x0042);
	snd_soc_write(codec, WM8900_REG_ADCPATH, 0x0055);

	reg = snd_soc_read(codec, WM8900_REG_DACCTRL);

	reg &= ~WM8900_REG_DACCTRL_MUTE;
	snd_soc_write(codec, WM8900_REG_DACCTRL, reg);

	snd_soc_write(codec, WM8900_REG_LOUT1CTL, 0x130);
	snd_soc_write(codec, WM8900_REG_ROUT1CTL, 0x130);

	/* Turn up vol slowly, for HP out pop noise */

	for (reg = 0; reg <= 0x33; reg += 0x10) {
			snd_soc_write(codec, WM8900_REG_LOUT2CTL, 0x100 + reg);
			snd_soc_write(codec, WM8900_REG_ROUT2CTL, 0x100 + reg);
			msleep(5);
	}
	snd_soc_write(codec, WM8900_REG_LOUT2CTL, 0x133);
	snd_soc_write(codec, WM8900_REG_ROUT2CTL, 0x133);

	msleep(20);

#ifdef SPK_CON
	if (isSPKon) {
		gpio_set_value(SPK_CON, GPIO_HIGH);
	}
#endif
#ifndef WM8900_NO_POWEROFF
	msleep(350);
	gpio_set_value(RK29_PIN1_PD6, GPIO_HIGH);
#endif
	wm8900_current_status |= WM8900_IS_STARTUP;
}

static void wm8900_work(struct work_struct *work)
{
        WM8900_DBG("Enter::wm8900_work : wm8900_work_type = %d\n", wm8900_work_type);

        switch (wm8900_work_type) {
        case WM8900_WORK_POWERDOWN_PLAYBACK :
                break;
        case WM8900_WORK_POWERDOWN_CAPTURE:
                snd_soc_write(wm8900_codec, WM8900_REG_POWER1, 0x210D);
                break;
        case WM8900_WORK_POWERDOWN_PLAYBACK_CAPTURE:
                wm8900_powerdown();
                break;
        case WM8900_WORK_HW_SET:
                wm8900_set_hw(wm8900_codec);
                break;
        default:
                break;
        }

        wm8900_work_type = WM8900_WORK_NULL;
}

static int wm8900_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct snd_soc_codec *codec = rtd->codec;
#else
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
#endif
	u16 reg;

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);

	reg = snd_soc_read(codec, WM8900_REG_AUDIO1) & ~0x60;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		reg |= 0x20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		reg |= 0x40;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		reg |= 0x60;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8900_REG_AUDIO1, reg);

	return 0;
}

/* FLL divisors */
struct _fll_div {
	u16 fll_ratio;
	u16 fllclk_div;
	u16 fll_slow_lock_ref;
	u16 n;
	u16 k;
};

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod, target;
	unsigned int div;

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);

	BUG_ON(!Fout);
        
	/* The FLL must run at 90-100MHz which is then scaled down to
	 * the output value by FLLCLK_DIV. */
	target = Fout;
	div = 1;
	while (target < 90000000) {
		div *= 2;
		target *= 2;
	}

	if (target > 100000000)
		printk(KERN_WARNING "wm8900: FLL rate %u out of range, Fref=%u"
		       " Fout=%u\n", target, Fref, Fout);
	if (div > 32) {
		printk(KERN_ERR "wm8900: Invalid FLL division rate %u, "
		       "Fref=%u, Fout=%u, target=%u\n",
		       div, Fref, Fout, target);
		return -EINVAL;
	}

	fll_div->fllclk_div = div >> 2;

	if (Fref < 48000)
		fll_div->fll_slow_lock_ref = 1;
	else
		fll_div->fll_slow_lock_ref = 0;

	Ndiv = target / Fref;

	if (Fref < 1000000)
		fll_div->fll_ratio = 8;
	else
		fll_div->fll_ratio = 1;

	fll_div->n = Ndiv / fll_div->fll_ratio;
	Nmod = (target / fll_div->fll_ratio) % Fref;

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, Fref);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll_div->k = K / 10;

	BUG_ON(target != Fout * (fll_div->fllclk_div << 2));
	BUG_ON(!K && target != Fref * fll_div->fll_ratio * fll_div->n);

	return 0;
}

static int wm8900_set_fll(struct snd_soc_codec *codec,
	int fll_id, unsigned int freq_in, unsigned int freq_out)
{
	struct wm8900_priv *wm8900 = snd_soc_codec_get_drvdata(codec);
	struct _fll_div fll_div;
	unsigned int reg;

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);

	if (wm8900->fll_in == freq_in && wm8900->fll_out == freq_out)
		return 0;

	/* The digital side should be disabled during any change. */
	reg = snd_soc_read(codec, WM8900_REG_POWER1);
	snd_soc_write(codec, WM8900_REG_POWER1,
		     reg & (~WM8900_REG_POWER1_FLL_ENA));

	/* Disable the FLL? */
	if (!freq_in || !freq_out) {
		reg = snd_soc_read(codec, WM8900_REG_CLOCKING1);
		snd_soc_write(codec, WM8900_REG_CLOCKING1,
			     reg & (~WM8900_REG_CLOCKING1_MCLK_SRC));

		reg = snd_soc_read(codec, WM8900_REG_FLLCTL1);
		snd_soc_write(codec, WM8900_REG_FLLCTL1,
			     reg & (~WM8900_REG_FLLCTL1_OSC_ENA));

		wm8900->fll_in = freq_in;
		wm8900->fll_out = freq_out;

		return 0;
	}

	if (fll_factors(&fll_div, freq_in, freq_out) != 0)
		goto reenable;

	wm8900->fll_in = freq_in;
	wm8900->fll_out = freq_out;

	/* The osclilator *MUST* be enabled before we enable the
	 * digital circuit. */
	snd_soc_write(codec, WM8900_REG_FLLCTL1,
		     fll_div.fll_ratio | WM8900_REG_FLLCTL1_OSC_ENA);

	snd_soc_write(codec, WM8900_REG_FLLCTL4, fll_div.n >> 5);
	snd_soc_write(codec, WM8900_REG_FLLCTL5,
		     (fll_div.fllclk_div << 6) | (fll_div.n & 0x1f));

	if (fll_div.k) {
		snd_soc_write(codec, WM8900_REG_FLLCTL2,
			     (fll_div.k >> 8) | 0x100);
		snd_soc_write(codec, WM8900_REG_FLLCTL3, fll_div.k & 0xff);
	} else
		snd_soc_write(codec, WM8900_REG_FLLCTL2, 0);

	if (fll_div.fll_slow_lock_ref)
		snd_soc_write(codec, WM8900_REG_FLLCTL6,
			     WM8900_REG_FLLCTL6_FLL_SLOW_LOCK_REF);
	else
		snd_soc_write(codec, WM8900_REG_FLLCTL6, 0);

	reg = snd_soc_read(codec, WM8900_REG_POWER1);
	snd_soc_write(codec, WM8900_REG_POWER1,
		     reg | WM8900_REG_POWER1_FLL_ENA);

reenable:
	reg = snd_soc_read(codec, WM8900_REG_CLOCKING1);
	snd_soc_write(codec, WM8900_REG_CLOCKING1,
		     reg | WM8900_REG_CLOCKING1_MCLK_SRC);

	return 0;
}

static int wm8900_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);

	return wm8900_set_fll(codec_dai->codec, pll_id, freq_in, freq_out);
}

static int wm8900_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
				 int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int reg;

	WM8900_DBG("Enter:%s, %d, div_id=%d, div=%d \n", __FUNCTION__, __LINE__, div_id, div);

	switch (div_id) {
	case WM8900_BCLK_DIV:
		reg = snd_soc_read(codec, WM8900_REG_CLOCKING1);
		snd_soc_write(codec, WM8900_REG_CLOCKING1,
			     div | (reg & WM8900_REG_CLOCKING1_BCLK_MASK));
		break;
	case WM8900_OPCLK_DIV:
		reg = snd_soc_read(codec, WM8900_REG_CLOCKING1);
		snd_soc_write(codec, WM8900_REG_CLOCKING1,
			     div | (reg & WM8900_REG_CLOCKING1_OPCLK_MASK));
		break;
	case WM8900_DAC_LRCLK:
		reg = snd_soc_read(codec, WM8900_REG_AUDIO4);
		snd_soc_write(codec, WM8900_REG_AUDIO4,
			     div | (reg & WM8900_LRC_MASK));
		break;
	case WM8900_ADC_LRCLK:
		reg = snd_soc_read(codec, WM8900_REG_AUDIO3);
		snd_soc_write(codec, WM8900_REG_AUDIO3,
			     div | (reg & WM8900_LRC_MASK));
		break;
	case WM8900_DAC_CLKDIV:
		reg = snd_soc_read(codec, WM8900_REG_CLOCKING2);
		snd_soc_write(codec, WM8900_REG_CLOCKING2,
			     div | (reg & WM8900_REG_CLOCKING2_DAC_CLKDIV));
		break;
	case WM8900_ADC_CLKDIV:
		reg = snd_soc_read(codec, WM8900_REG_CLOCKING2);
		snd_soc_write(codec, WM8900_REG_CLOCKING2,
			     div | (reg & WM8900_REG_CLOCKING2_ADC_CLKDIV));
		break;
	case WM8900_LRCLK_MODE:
		reg = snd_soc_read(codec, WM8900_REG_DACCTRL);
		snd_soc_write(codec, WM8900_REG_DACCTRL,
			     div | (reg & WM8900_REG_DACCTRL_AIF_LRCLKRATE));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int wm8900_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int clocking1, aif1, aif3, aif4;

	WM8900_DBG("Enter:%s, %d, fmt=0x%08X \n", __FUNCTION__, __LINE__, fmt);

	clocking1 = snd_soc_read(codec, WM8900_REG_CLOCKING1);
	aif1 = snd_soc_read(codec, WM8900_REG_AUDIO1);
	aif3 = snd_soc_read(codec, WM8900_REG_AUDIO3);
	aif4 = snd_soc_read(codec, WM8900_REG_AUDIO4);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		clocking1 &= ~WM8900_REG_CLOCKING1_BCLK_DIR;
		aif3 &= ~WM8900_REG_AUDIO3_ADCLRC_DIR;
		aif4 &= ~WM8900_REG_AUDIO4_DACLRC_DIR;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		clocking1 &= ~WM8900_REG_CLOCKING1_BCLK_DIR;
		aif3 |= WM8900_REG_AUDIO3_ADCLRC_DIR;
		aif4 |= WM8900_REG_AUDIO4_DACLRC_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		clocking1 |= WM8900_REG_CLOCKING1_BCLK_DIR;
		aif3 |= WM8900_REG_AUDIO3_ADCLRC_DIR;
		aif4 |= WM8900_REG_AUDIO4_DACLRC_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		clocking1 |= WM8900_REG_CLOCKING1_BCLK_DIR;
		aif3 &= ~WM8900_REG_AUDIO3_ADCLRC_DIR;
		aif4 &= ~WM8900_REG_AUDIO4_DACLRC_DIR;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		aif1 |= WM8900_REG_AUDIO1_AIF_FMT_MASK;
		aif1 &= ~WM8900_REG_AUDIO1_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		aif1 |= WM8900_REG_AUDIO1_AIF_FMT_MASK;
		aif1 |= WM8900_REG_AUDIO1_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif1 &= ~WM8900_REG_AUDIO1_AIF_FMT_MASK;
		aif1 |= 0x10;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		aif1 &= ~WM8900_REG_AUDIO1_AIF_FMT_MASK;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif1 &= ~WM8900_REG_AUDIO1_AIF_FMT_MASK;
		aif1 |= 0x8;
		break;
	default:
		return -EINVAL;
	}

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			aif1 &= ~WM8900_REG_AUDIO1_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8900_REG_AUDIO1_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			aif1 &= ~WM8900_REG_AUDIO1_BCLK_INV;
			aif1 &= ~WM8900_REG_AUDIO1_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif1 |= WM8900_REG_AUDIO1_BCLK_INV;
			aif1 |= WM8900_REG_AUDIO1_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8900_REG_AUDIO1_BCLK_INV;
			aif1 &= ~WM8900_REG_AUDIO1_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif1 &= ~WM8900_REG_AUDIO1_BCLK_INV;
			aif1 |= WM8900_REG_AUDIO1_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8900_REG_CLOCKING1, clocking1);
	snd_soc_write(codec, WM8900_REG_AUDIO1, aif1);
	snd_soc_write(codec, WM8900_REG_AUDIO3, aif3);
	snd_soc_write(codec, WM8900_REG_AUDIO4, aif4);

	return 0;
}

static int wm8900_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	WM8900_DBG("Enter:%s, %d , mute = %d \n", __FUNCTION__, __LINE__, mute);

	return 0;
}

static int wm8900_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
#else
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_dai *codec_dai = machine->codec_dai;
#endif

	WM8900_DBG("Enter::%s----%d substream->stream:%s \n",__FUNCTION__,__LINE__,
		   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE");

	cancel_delayed_work_sync(&delayed_work);
	wm8900_work_type = WM8900_WORK_NULL;

	wm8900_set_hw(codec);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE ||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	     codec_dai->capture_active) {
#else
	     codec_dai->capture.active) {
#endif
		snd_soc_write(codec, WM8900_REG_POWER1, 0x211D);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	} else if (!codec_dai->capture_active) {
#else
	} else if (!codec_dai->capture.active) {
#endif
		snd_soc_write(codec, WM8900_REG_POWER1, 0x210D);
	}

	return 0;
}

static void wm8900_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
#else
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_dai *codec_dai = machine->codec_dai;
#endif

	WM8900_DBG("Enter::%s----%d substream->stream:%s \n",__FUNCTION__,__LINE__,
		   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE");

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
	    wm8900_work_type == WM8900_WORK_NULL) {
		cancel_delayed_work_sync(&delayed_work);
		wm8900_work_type = WM8900_WORK_POWERDOWN_CAPTURE;
		queue_delayed_work(wm8900_workq, &delayed_work,
			msecs_to_jiffies(3000));
	}
#ifdef WM8900_NO_POWEROFF
	return; /* Let codec not going to power off for pop noise */
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	if (!codec_dai->capture_active && !codec_dai->playback_active) {
#else
	if (!codec_dai->capture.active && !codec_dai->playback.active) {
#endif

		cancel_delayed_work_sync(&delayed_work);
		wm8900_work_type = WM8900_WORK_NULL;

		/* If codec is already shutdown, return */
		if (wm8900_current_status == WM8900_IS_SHUTDOWN)
			return;

		WM8900_DBG("Is going to power down wm8900\n");

		wm8900_work_type = WM8900_WORK_POWERDOWN_PLAYBACK_CAPTURE;

		/* If codec is useless, queue work to close it */
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			queue_delayed_work(wm8900_workq, &delayed_work,
				msecs_to_jiffies(1000));
		} else {
			queue_delayed_work(wm8900_workq, &delayed_work,
				msecs_to_jiffies(3000));
		}
	}
}

static int wm8900_trigger(struct snd_pcm_substream *substream,
			  int status,
			  struct snd_soc_dai *dai)
{	
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
#else
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_dai *codec_dai = machine->codec_dai;
#endif

	WM8900_DBG("Enter::%s----%d status = %d substream->stream:%s \n",__FUNCTION__, __LINE__, status,
		   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE");	

	if(status == 1 || status == 0){
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
			codec_dai->playback_active = status;
#else
			codec_dai->playback.active = status;
#endif
		}else{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
			codec_dai->capture_active = status;
#else
			codec_dai->capture.active = status;
#endif
		}
	}

	return 0;
}

#define WM8900_RATES SNDRV_PCM_RATE_44100

#define WM8900_PCM_FORMATS \
	(SNDRV_PCM_FORMAT_S16_LE | SNDRV_PCM_FORMAT_S20_3LE | \
	 SNDRV_PCM_FORMAT_S24_LE)

static struct snd_soc_dai_ops wm8900_dai_ops = {
	.hw_params	= wm8900_hw_params,
	.set_clkdiv	= wm8900_set_dai_clkdiv,
	.set_pll	= wm8900_set_dai_pll,
	.set_fmt	= wm8900_set_dai_fmt,
	.digital_mute	= wm8900_digital_mute,
	.startup	= wm8900_startup,
	.shutdown	= wm8900_shutdown,
	.trigger	= wm8900_trigger,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static struct snd_soc_dai_driver wm8900_dai = {
#else
struct snd_soc_dai wm8900_dai = {
#endif
	.name = "WM8900 HiFi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8900_RATES,
		.formats = WM8900_PCM_FORMATS,
	},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8900_RATES,
		.formats = WM8900_PCM_FORMATS,
	 },
	.ops = &wm8900_dai_ops,
};
EXPORT_SYMBOL_GPL(wm8900_dai);

static int wm8900_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 reg;

	WM8900_DBG("Enter:%s, %d, level=0x%08X \n", __FUNCTION__, __LINE__, level);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	codec->dapm.bias_level = level;
#else
	codec->bias_level = level;
#endif
	return 0;
#if 0

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* Enable thermal shutdown */
		reg = snd_soc_read(codec, WM8900_REG_GPIO);
		snd_soc_write(codec, WM8900_REG_GPIO,
			     reg | WM8900_REG_GPIO_TEMP_ENA);
		reg = snd_soc_read(codec, WM8900_REG_ADDCTL);
		snd_soc_write(codec, WM8900_REG_ADDCTL,
			     reg | WM8900_REG_ADDCTL_TEMP_SD);
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		/* Charge capacitors if initial power up */
		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			/* STARTUP_BIAS_ENA on */
			snd_soc_write(codec, WM8900_REG_POWER1,
				     WM8900_REG_POWER1_STARTUP_BIAS_ENA);

			/* Startup bias mode */
			snd_soc_write(codec, WM8900_REG_ADDCTL,
				     WM8900_REG_ADDCTL_BIAS_SRC |
				     WM8900_REG_ADDCTL_VMID_SOFTST);

			/* VMID 2x50k */
			snd_soc_write(codec, WM8900_REG_POWER1,
				     WM8900_REG_POWER1_STARTUP_BIAS_ENA | 0x1);

			/* Allow capacitors to charge */
			schedule_timeout_interruptible(msecs_to_jiffies(400));

			/* Enable bias */
			snd_soc_write(codec, WM8900_REG_POWER1,
				     WM8900_REG_POWER1_STARTUP_BIAS_ENA |
				     WM8900_REG_POWER1_BIAS_ENA | 0x1);

			snd_soc_write(codec, WM8900_REG_ADDCTL, 0);

			snd_soc_write(codec, WM8900_REG_POWER1,
				     WM8900_REG_POWER1_BIAS_ENA | 0x1);
		}

		reg = snd_soc_read(codec, WM8900_REG_POWER1);
		snd_soc_write(codec, WM8900_REG_POWER1,
			     (reg & WM8900_REG_POWER1_FLL_ENA) |
			     WM8900_REG_POWER1_BIAS_ENA | 0x1);
		snd_soc_write(codec, WM8900_REG_POWER2,
			     WM8900_REG_POWER2_SYSCLK_ENA);
		snd_soc_write(codec, WM8900_REG_POWER3, 0);
		break;

	case SND_SOC_BIAS_OFF:
		/* Startup bias enable */
		reg = snd_soc_read(codec, WM8900_REG_POWER1);
		snd_soc_write(codec, WM8900_REG_POWER1,
			     reg & WM8900_REG_POWER1_STARTUP_BIAS_ENA);
		snd_soc_write(codec, WM8900_REG_ADDCTL,
			     WM8900_REG_ADDCTL_BIAS_SRC |
			     WM8900_REG_ADDCTL_VMID_SOFTST);

		/* Discharge caps */
		snd_soc_write(codec, WM8900_REG_POWER1,
			     WM8900_REG_POWER1_STARTUP_BIAS_ENA);
		schedule_timeout_interruptible(msecs_to_jiffies(500));

		/* Remove clamp */
		snd_soc_write(codec, WM8900_REG_HPCTL1, 0);

		/* Power down */
		snd_soc_write(codec, WM8900_REG_ADDCTL, 0);
		snd_soc_write(codec, WM8900_REG_POWER1, 0);
		snd_soc_write(codec, WM8900_REG_POWER2, 0);
		snd_soc_write(codec, WM8900_REG_POWER3, 0);

		/* Need to let things settle before stopping the clock
		 * to ensure that restart works, see "Stopping the
		 * master clock" in the datasheet. */
		schedule_timeout_interruptible(msecs_to_jiffies(1));
		snd_soc_write(codec, WM8900_REG_POWER2,
			     WM8900_REG_POWER2_SYSCLK_ENA);
		break;
	}

	codec->bias_level = level;
	return 0;
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int wm8900_suspend(struct snd_soc_codec *codec, pm_message_t state)
#else
static int wm8900_suspend(struct platform_device *pdev, pm_message_t state)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
#endif
	struct wm8900_priv *wm8900 = snd_soc_codec_get_drvdata(codec);
	int fll_out = wm8900->fll_out;
	int fll_in  = wm8900->fll_in;
	int ret;

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);

	cancel_delayed_work_sync(&delayed_work);
	wm8900_work_type = WM8900_WORK_NULL;

#ifdef WM8900_NO_POWEROFF
	wm8900_powerdown();
#endif

	/* Stop the FLL in an orderly fashion */
	ret = wm8900_set_fll(codec, 0, 0, 0);
	if (ret != 0) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		dev_err(codec->dev, "Failed to stop FLL\n");
#else
		dev_err(&pdev->dev, "Failed to stop FLL\n");
#endif
		return ret;
	}

	wm8900->fll_out = fll_out;
	wm8900->fll_in = fll_in;

	wm8900_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int wm8900_resume(struct snd_soc_codec *codec)
#else
static int wm8900_resume(struct platform_device *pdev)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
#endif
	struct wm8900_priv *wm8900 = snd_soc_codec_get_drvdata(codec);

	wm8900_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Restart the FLL? */
	if (wm8900->fll_out) {
		int ret;
		int fll_out = wm8900->fll_out;
		int fll_in  = wm8900->fll_in;

		wm8900->fll_in = 0;
		wm8900->fll_out = 0;

		ret = wm8900_set_fll(codec, 0, fll_in, fll_out);
		if (ret != 0) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
			dev_err(codec->dev, "Failed to restart FLL\n");
#else
			dev_err(&pdev->dev, "Failed to restart FLL\n");
#endif
			return ret;
		}
	}

#ifdef WM8900_NO_POWEROFF
	if (wm8900_current_status == WM8900_IS_SHUTDOWN) {

		cancel_delayed_work_sync(&delayed_work);
		wm8900_work_type = WM8900_WORK_HW_SET;
		queue_delayed_work(wm8900_workq, &delayed_work,
		                   msecs_to_jiffies(1000));
	}
#endif

	return 0;
}

#if 0
static __devinit int wm8900_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8900_priv *wm8900;
	struct snd_soc_codec *codec;
	unsigned int reg;
	int ret;

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);
        
	wm8900 = kzalloc(sizeof(struct wm8900_priv), GFP_KERNEL);
	if (wm8900 == NULL)
		return -ENOMEM;

	codec = &wm8900->codec;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	snd_soc_codec_set_drvdata(codec, wm8900);
	codec->reg_cache = &wm8900->reg_cache[0];
	codec->reg_cache_size = WM8900_MAXREG;
#endif

	mutex_init(&codec->mutex);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
#endif

	codec->name = "WM8900";
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	codec->owner = THIS_MODULE;
	codec->dai = &wm8900_dai;
#endif
	codec->num_dai = 1;
	codec->control_data = i2c;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	codec->set_bias_level = wm8900_set_bias_level;
	codec->volatile_register = wm8900_volatile_register;
#endif
	codec->dev = &i2c->dev;

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	reg = snd_soc_read(codec, WM8900_REG_ID);
	if (reg != 0x8900) {
		dev_err(&i2c->dev, "Device is not a WM8900 - ID %x\n", reg);
		ret = -ENODEV;
		goto err;
	}

	wm8900_reset(codec);

	/* Turn the chip on */
	wm8900_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	wm8900_dai.dev = &i2c->dev;
#endif

	wm8900_codec = codec;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8900, &wm8900_dai, 1);
#else
	ret = snd_soc_register_codec(codec);
#endif
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);
		goto err;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	ret = snd_soc_register_dai(&wm8900_dai);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register DAI: %d\n", ret);
		goto err_codec;
	}
#endif

	return ret;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
err_codec:
	snd_soc_unregister_codec(codec);
#endif
err:
	kfree(wm8900);
	wm8900_codec = NULL;
	return ret;
}
#else
static __devinit int wm8900_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8900_priv *wm8900;
	int ret;

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);

	wm8900 = kzalloc(sizeof(struct wm8900_priv), GFP_KERNEL);
	if (wm8900 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8900);
	wm8900->control_type = SND_SOC_I2C;

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8900, &wm8900_dai, 1);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);
		kfree(wm8900);
	}
	return ret;
}
#endif

static __devexit int wm8900_i2c_remove(struct i2c_client *client)
{
	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
#else
	snd_soc_unregister_dai(&wm8900_dai);
	snd_soc_unregister_codec(wm8900_codec);

	wm8900_set_bias_level(wm8900_codec, SND_SOC_BIAS_OFF);

	wm8900_dai.dev = NULL;
	kfree(snd_soc_codec_get_drvdata(wm8900_codec));
	wm8900_codec = NULL;
#endif

	return 0;
}

void wm8900_i2c_shutdown(struct i2c_client *client)
{
	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);
	wm8900_powerdown();
}

static const struct i2c_device_id wm8900_i2c_id[] = {
	{ "wm8900", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8900_i2c_id);

static struct i2c_driver wm8900_i2c_driver = {
	.driver = {
		.name = "WM8900",
		.owner = THIS_MODULE,
	},
	.probe = wm8900_i2c_probe,
	.remove = __devexit_p(wm8900_i2c_remove),
	.shutdown = wm8900_i2c_shutdown,
	.id_table = wm8900_i2c_id,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int wm8900_probe(struct snd_soc_codec *codec)
{
	struct wm8900_priv *wm8900 = snd_soc_codec_get_drvdata(codec);
	int ret;
	wm8900_codec = codec;

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, wm8900->control_type);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
#else
static int wm8900_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;
#endif

#ifndef WM8900_NO_POWEROFF
	gpio_set_value(RK29_PIN1_PD6, GPIO_LOW);
#endif

	WM8900_DBG("Enter:%s, %d \n", __FUNCTION__, __LINE__);
        
	if (!wm8900_codec) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		dev_err(codec->dev, "I2C client not yet instantiated\n");
#else
		dev_err(&pdev->dev, "I2C client not yet instantiated\n");
#endif
		return -ENODEV;
	}

#if defined(SPK_CON)
	gpio_request(SPK_CON,NULL);
	gpio_direction_output(SPK_CON, GPIO_LOW);
#endif

	codec = wm8900_codec;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	socdev->card->codec = codec;

	/* Register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to register new PCMs\n");
		dev_err(&pdev->dev, "Failed to register new PCMs\n");
		goto pcm_err;
	}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
	wm8900_workq = create_freezable_workqueue("wm8900");
#else
	wm8900_workq = create_freezeable_workqueue("wm8900");
#endif
	if (wm8900_workq == NULL) {
		kfree(codec);
		return -ENOMEM;
	}

#ifdef WM8900_NO_POWEROFF
	wm8900_set_hw(codec);
#endif

	return ret;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
pcm_err:
	return ret;
#endif
}

/* power down chip */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int wm8900_remove(struct snd_soc_codec *codec)
{
	wm8900_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}
#else
static int wm8900_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static struct snd_soc_codec_driver soc_codec_dev_wm8900 = {
	.probe =	wm8900_probe,
	.remove =	wm8900_remove,
	.suspend =	wm8900_suspend,
	.resume =	wm8900_resume,
	.set_bias_level = wm8900_set_bias_level,
	.volatile_register = wm8900_volatile_register,
	.reg_cache_size = ARRAY_SIZE(wm8900_reg_defaults),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8900_reg_defaults,
};
#else
struct snd_soc_codec_device soc_codec_dev_wm8900 = {
	.probe = 	wm8900_probe,
	.remove = 	wm8900_remove,
	.suspend = 	wm8900_suspend,
	.resume =	wm8900_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8900);
#endif

static int __init wm8900_modinit(void)
{
	return i2c_add_driver(&wm8900_i2c_driver);
}
module_init(wm8900_modinit);

static void __exit wm8900_exit(void)
{
	i2c_del_driver(&wm8900_i2c_driver);
}
module_exit(wm8900_exit);

MODULE_DESCRIPTION("ASoC WM8900 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfonmicro.com>");
MODULE_LICENSE("GPL");
