/*
 * Nuvoton NAU8825 audio codec driver
 *
 * Copyright 2015 Google Chromium project.
 *  Author: Anatol Pomozov <anatol@chromium.org>
 * Copyright 2015 Nuvoton Technology Corp.
 *  Co-author: Meng-Huang Kuo <mhkuo@nuvoton.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/acpi.h>
#include <linux/math64.h>
#include <linux/semaphore.h>

#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>


#include "nau8825.h"


#define NUVOTON_CODEC_DAI "nau8825-hifi"

#define NAU_FREF_MAX 13500000
#define NAU_FVCO_MAX 124000000
#define NAU_FVCO_MIN 90000000

/* cross talk suppression detection */
#define LOG10_MAGIC 646456993
#define GAIN_AUGMENT 22500
#define SIDETONE_BASE 207000

/* the maximum frequency of CLK_ADC and CLK_DAC */
#define CLK_DA_AD_MAX 6144000

static int nau8825_configure_sysclk(struct nau8825 *nau8825,
		int clk_id, unsigned int freq);

struct nau8825_fll {
	int mclk_src;
	int ratio;
	int fll_frac;
	int fll_int;
	int clk_ref_div;
};

struct nau8825_fll_attr {
	unsigned int param;
	unsigned int val;
};

/* scaling for mclk from sysclk_src output */
static const struct nau8825_fll_attr mclk_src_scaling[] = {
	{ 1, 0x0 },
	{ 2, 0x2 },
	{ 4, 0x3 },
	{ 8, 0x4 },
	{ 16, 0x5 },
	{ 32, 0x6 },
	{ 3, 0x7 },
	{ 6, 0xa },
	{ 12, 0xb },
	{ 24, 0xc },
	{ 48, 0xd },
	{ 96, 0xe },
	{ 5, 0xf },
};

/* ratio for input clk freq */
static const struct nau8825_fll_attr fll_ratio[] = {
	{ 512000, 0x01 },
	{ 256000, 0x02 },
	{ 128000, 0x04 },
	{ 64000, 0x08 },
	{ 32000, 0x10 },
	{ 8000, 0x20 },
	{ 4000, 0x40 },
};

static const struct nau8825_fll_attr fll_pre_scalar[] = {
	{ 1, 0x0 },
	{ 2, 0x1 },
	{ 4, 0x2 },
	{ 8, 0x3 },
};

/* over sampling rate */
struct nau8825_osr_attr {
	unsigned int osr;
	unsigned int clk_src;
};

static const struct nau8825_osr_attr osr_dac_sel[] = {
	{ 64, 2 },	/* OSR 64, SRC 1/4 */
	{ 256, 0 },	/* OSR 256, SRC 1 */
	{ 128, 1 },	/* OSR 128, SRC 1/2 */
	{ 0, 0 },
	{ 32, 3 },	/* OSR 32, SRC 1/8 */
};

static const struct nau8825_osr_attr osr_adc_sel[] = {
	{ 32, 3 },	/* OSR 32, SRC 1/8 */
	{ 64, 2 },	/* OSR 64, SRC 1/4 */
	{ 128, 1 },	/* OSR 128, SRC 1/2 */
	{ 256, 0 },	/* OSR 256, SRC 1 */
};

static const struct reg_default nau8825_reg_defaults[] = {
	{ NAU8825_REG_ENA_CTRL, 0x00ff },
	{ NAU8825_REG_IIC_ADDR_SET, 0x0 },
	{ NAU8825_REG_CLK_DIVIDER, 0x0050 },
	{ NAU8825_REG_FLL1, 0x0 },
	{ NAU8825_REG_FLL2, 0x3126 },
	{ NAU8825_REG_FLL3, 0x0008 },
	{ NAU8825_REG_FLL4, 0x0010 },
	{ NAU8825_REG_FLL5, 0x0 },
	{ NAU8825_REG_FLL6, 0x6000 },
	{ NAU8825_REG_FLL_VCO_RSV, 0xf13c },
	{ NAU8825_REG_HSD_CTRL, 0x000c },
	{ NAU8825_REG_JACK_DET_CTRL, 0x0 },
	{ NAU8825_REG_INTERRUPT_MASK, 0x0 },
	{ NAU8825_REG_INTERRUPT_DIS_CTRL, 0xffff },
	{ NAU8825_REG_SAR_CTRL, 0x0015 },
	{ NAU8825_REG_KEYDET_CTRL, 0x0110 },
	{ NAU8825_REG_VDET_THRESHOLD_1, 0x0 },
	{ NAU8825_REG_VDET_THRESHOLD_2, 0x0 },
	{ NAU8825_REG_VDET_THRESHOLD_3, 0x0 },
	{ NAU8825_REG_VDET_THRESHOLD_4, 0x0 },
	{ NAU8825_REG_GPIO34_CTRL, 0x0 },
	{ NAU8825_REG_GPIO12_CTRL, 0x0 },
	{ NAU8825_REG_TDM_CTRL, 0x0 },
	{ NAU8825_REG_I2S_PCM_CTRL1, 0x000b },
	{ NAU8825_REG_I2S_PCM_CTRL2, 0x8010 },
	{ NAU8825_REG_LEFT_TIME_SLOT, 0x0 },
	{ NAU8825_REG_RIGHT_TIME_SLOT, 0x0 },
	{ NAU8825_REG_BIQ_CTRL, 0x0 },
	{ NAU8825_REG_BIQ_COF1, 0x0 },
	{ NAU8825_REG_BIQ_COF2, 0x0 },
	{ NAU8825_REG_BIQ_COF3, 0x0 },
	{ NAU8825_REG_BIQ_COF4, 0x0 },
	{ NAU8825_REG_BIQ_COF5, 0x0 },
	{ NAU8825_REG_BIQ_COF6, 0x0 },
	{ NAU8825_REG_BIQ_COF7, 0x0 },
	{ NAU8825_REG_BIQ_COF8, 0x0 },
	{ NAU8825_REG_BIQ_COF9, 0x0 },
	{ NAU8825_REG_BIQ_COF10, 0x0 },
	{ NAU8825_REG_ADC_RATE, 0x0010 },
	{ NAU8825_REG_DAC_CTRL1, 0x0001 },
	{ NAU8825_REG_DAC_CTRL2, 0x0 },
	{ NAU8825_REG_DAC_DGAIN_CTRL, 0x0 },
	{ NAU8825_REG_ADC_DGAIN_CTRL, 0x00cf },
	{ NAU8825_REG_MUTE_CTRL, 0x0 },
	{ NAU8825_REG_HSVOL_CTRL, 0x0 },
	{ NAU8825_REG_DACL_CTRL, 0x02cf },
	{ NAU8825_REG_DACR_CTRL, 0x00cf },
	{ NAU8825_REG_ADC_DRC_KNEE_IP12, 0x1486 },
	{ NAU8825_REG_ADC_DRC_KNEE_IP34, 0x0f12 },
	{ NAU8825_REG_ADC_DRC_SLOPES, 0x25ff },
	{ NAU8825_REG_ADC_DRC_ATKDCY, 0x3457 },
	{ NAU8825_REG_DAC_DRC_KNEE_IP12, 0x1486 },
	{ NAU8825_REG_DAC_DRC_KNEE_IP34, 0x0f12 },
	{ NAU8825_REG_DAC_DRC_SLOPES, 0x25f9 },
	{ NAU8825_REG_DAC_DRC_ATKDCY, 0x3457 },
	{ NAU8825_REG_IMM_MODE_CTRL, 0x0 },
	{ NAU8825_REG_CLASSG_CTRL, 0x0 },
	{ NAU8825_REG_OPT_EFUSE_CTRL, 0x0 },
	{ NAU8825_REG_MISC_CTRL, 0x0 },
	{ NAU8825_REG_BIAS_ADJ, 0x0 },
	{ NAU8825_REG_TRIM_SETTINGS, 0x0 },
	{ NAU8825_REG_ANALOG_CONTROL_1, 0x0 },
	{ NAU8825_REG_ANALOG_CONTROL_2, 0x0 },
	{ NAU8825_REG_ANALOG_ADC_1, 0x0011 },
	{ NAU8825_REG_ANALOG_ADC_2, 0x0020 },
	{ NAU8825_REG_RDAC, 0x0008 },
	{ NAU8825_REG_MIC_BIAS, 0x0006 },
	{ NAU8825_REG_BOOST, 0x0 },
	{ NAU8825_REG_FEPGA, 0x0 },
	{ NAU8825_REG_POWER_UP_CONTROL, 0x0 },
	{ NAU8825_REG_CHARGE_PUMP, 0x0 },
};

/* register backup table when cross talk detection */
static struct reg_default nau8825_xtalk_baktab[] = {
	{ NAU8825_REG_ADC_DGAIN_CTRL, 0 },
	{ NAU8825_REG_HSVOL_CTRL, 0 },
	{ NAU8825_REG_DACL_CTRL, 0 },
	{ NAU8825_REG_DACR_CTRL, 0 },
};

static const unsigned short logtable[256] = {
	0x0000, 0x0171, 0x02e0, 0x044e, 0x05ba, 0x0725, 0x088e, 0x09f7,
	0x0b5d, 0x0cc3, 0x0e27, 0x0f8a, 0x10eb, 0x124b, 0x13aa, 0x1508,
	0x1664, 0x17bf, 0x1919, 0x1a71, 0x1bc8, 0x1d1e, 0x1e73, 0x1fc6,
	0x2119, 0x226a, 0x23ba, 0x2508, 0x2656, 0x27a2, 0x28ed, 0x2a37,
	0x2b80, 0x2cc8, 0x2e0f, 0x2f54, 0x3098, 0x31dc, 0x331e, 0x345f,
	0x359f, 0x36de, 0x381b, 0x3958, 0x3a94, 0x3bce, 0x3d08, 0x3e41,
	0x3f78, 0x40af, 0x41e4, 0x4319, 0x444c, 0x457f, 0x46b0, 0x47e1,
	0x4910, 0x4a3f, 0x4b6c, 0x4c99, 0x4dc5, 0x4eef, 0x5019, 0x5142,
	0x526a, 0x5391, 0x54b7, 0x55dc, 0x5700, 0x5824, 0x5946, 0x5a68,
	0x5b89, 0x5ca8, 0x5dc7, 0x5ee5, 0x6003, 0x611f, 0x623a, 0x6355,
	0x646f, 0x6588, 0x66a0, 0x67b7, 0x68ce, 0x69e4, 0x6af8, 0x6c0c,
	0x6d20, 0x6e32, 0x6f44, 0x7055, 0x7165, 0x7274, 0x7383, 0x7490,
	0x759d, 0x76aa, 0x77b5, 0x78c0, 0x79ca, 0x7ad3, 0x7bdb, 0x7ce3,
	0x7dea, 0x7ef0, 0x7ff6, 0x80fb, 0x81ff, 0x8302, 0x8405, 0x8507,
	0x8608, 0x8709, 0x8809, 0x8908, 0x8a06, 0x8b04, 0x8c01, 0x8cfe,
	0x8dfa, 0x8ef5, 0x8fef, 0x90e9, 0x91e2, 0x92db, 0x93d2, 0x94ca,
	0x95c0, 0x96b6, 0x97ab, 0x98a0, 0x9994, 0x9a87, 0x9b7a, 0x9c6c,
	0x9d5e, 0x9e4f, 0x9f3f, 0xa02e, 0xa11e, 0xa20c, 0xa2fa, 0xa3e7,
	0xa4d4, 0xa5c0, 0xa6ab, 0xa796, 0xa881, 0xa96a, 0xaa53, 0xab3c,
	0xac24, 0xad0c, 0xadf2, 0xaed9, 0xafbe, 0xb0a4, 0xb188, 0xb26c,
	0xb350, 0xb433, 0xb515, 0xb5f7, 0xb6d9, 0xb7ba, 0xb89a, 0xb97a,
	0xba59, 0xbb38, 0xbc16, 0xbcf4, 0xbdd1, 0xbead, 0xbf8a, 0xc065,
	0xc140, 0xc21b, 0xc2f5, 0xc3cf, 0xc4a8, 0xc580, 0xc658, 0xc730,
	0xc807, 0xc8de, 0xc9b4, 0xca8a, 0xcb5f, 0xcc34, 0xcd08, 0xcddc,
	0xceaf, 0xcf82, 0xd054, 0xd126, 0xd1f7, 0xd2c8, 0xd399, 0xd469,
	0xd538, 0xd607, 0xd6d6, 0xd7a4, 0xd872, 0xd93f, 0xda0c, 0xdad9,
	0xdba5, 0xdc70, 0xdd3b, 0xde06, 0xded0, 0xdf9a, 0xe063, 0xe12c,
	0xe1f5, 0xe2bd, 0xe385, 0xe44c, 0xe513, 0xe5d9, 0xe69f, 0xe765,
	0xe82a, 0xe8ef, 0xe9b3, 0xea77, 0xeb3b, 0xebfe, 0xecc1, 0xed83,
	0xee45, 0xef06, 0xefc8, 0xf088, 0xf149, 0xf209, 0xf2c8, 0xf387,
	0xf446, 0xf505, 0xf5c3, 0xf680, 0xf73e, 0xf7fb, 0xf8b7, 0xf973,
	0xfa2f, 0xfaea, 0xfba5, 0xfc60, 0xfd1a, 0xfdd4, 0xfe8e, 0xff47
};

/**
 * nau8825_sema_acquire - acquire the semaphore of nau88l25
 * @nau8825:  component to register the codec private data with
 * @timeout: how long in jiffies to wait before failure or zero to wait
 * until release
 *
 * Attempts to acquire the semaphore with number of jiffies. If no more
 * tasks are allowed to acquire the semaphore, calling this function will
 * put the task to sleep. If the semaphore is not released within the
 * specified number of jiffies, this function returns.
 * Acquires the semaphore without jiffies. If no more tasks are allowed
 * to acquire the semaphore, calling this function will put the task to
 * sleep until the semaphore is released.
 * If the semaphore is not released within the specified number of jiffies,
 * this function returns -ETIME.
 * If the sleep is interrupted by a signal, this function will return -EINTR.
 * It returns 0 if the semaphore was acquired successfully.
 */
static int nau8825_sema_acquire(struct nau8825 *nau8825, long timeout)
{
	int ret;

	if (timeout) {
		ret = down_timeout(&nau8825->xtalk_sem, timeout);
		if (ret < 0)
			dev_warn(nau8825->dev, "Acquire semaphore timeout\n");
	} else {
		ret = down_interruptible(&nau8825->xtalk_sem);
		if (ret < 0)
			dev_warn(nau8825->dev, "Acquire semaphore fail\n");
	}

	return ret;
}

/**
 * nau8825_sema_release - release the semaphore of nau88l25
 * @nau8825:  component to register the codec private data with
 *
 * Release the semaphore which may be called from any context and
 * even by tasks which have never called down().
 */
static inline void nau8825_sema_release(struct nau8825 *nau8825)
{
	up(&nau8825->xtalk_sem);
}

/**
 * nau8825_sema_reset - reset the semaphore for nau88l25
 * @nau8825:  component to register the codec private data with
 *
 * Reset the counter of the semaphore. Call this function to restart
 * a new round task management.
 */
static inline void nau8825_sema_reset(struct nau8825 *nau8825)
{
	nau8825->xtalk_sem.count = 1;
}

/**
 * Ramp up the headphone volume change gradually to target level.
 *
 * @nau8825:  component to register the codec private data with
 * @vol_from: the volume to start up
 * @vol_to: the target volume
 * @step: the volume span to move on
 *
 * The headphone volume is from 0dB to minimum -54dB and -1dB per step.
 * If the volume changes sharp, there is a pop noise heard in headphone. We
 * provide the function to ramp up the volume up or down by delaying 10ms
 * per step.
 */
static void nau8825_hpvol_ramp(struct nau8825 *nau8825,
	unsigned int vol_from, unsigned int vol_to, unsigned int step)
{
	unsigned int value, volume, ramp_up, from, to;

	if (vol_from == vol_to || step == 0) {
		return;
	} else if (vol_from < vol_to) {
		ramp_up = true;
		from = vol_from;
		to = vol_to;
	} else {
		ramp_up = false;
		from = vol_to;
		to = vol_from;
	}
	/* only handle volume from 0dB to minimum -54dB */
	if (to > NAU8825_HP_VOL_MIN)
		to = NAU8825_HP_VOL_MIN;

	for (volume = from; volume < to; volume += step) {
		if (ramp_up)
			value = volume;
		else
			value = to - volume + from;
		regmap_update_bits(nau8825->regmap, NAU8825_REG_HSVOL_CTRL,
			NAU8825_HPL_VOL_MASK | NAU8825_HPR_VOL_MASK,
			(value << NAU8825_HPL_VOL_SFT) | value);
		usleep_range(10000, 10500);
	}
	if (ramp_up)
		value = to;
	else
		value = from;
	regmap_update_bits(nau8825->regmap, NAU8825_REG_HSVOL_CTRL,
		NAU8825_HPL_VOL_MASK | NAU8825_HPR_VOL_MASK,
		(value << NAU8825_HPL_VOL_SFT) | value);
}

/**
 * Computes log10 of a value; the result is round off to 3 decimal. This func-
 * tion takes reference to dvb-math. The source code locates as the following.
 * Linux/drivers/media/dvb-core/dvb_math.c
 *
 * return log10(value) * 1000
 */
static u32 nau8825_intlog10_dec3(u32 value)
{
	u32 msb, logentry, significand, interpolation, log10val;
	u64 log2val;

	/* first detect the msb (count begins at 0) */
	msb = fls(value) - 1;
	/**
	 *      now we use a logtable after the following method:
	 *
	 *      log2(2^x * y) * 2^24 = x * 2^24 + log2(y) * 2^24
	 *      where x = msb and therefore 1 <= y < 2
	 *      first y is determined by shifting the value left
	 *      so that msb is bit 31
	 *              0x00231f56 -> 0x8C7D5800
	 *      the result is y * 2^31 -> "significand"
	 *      then the highest 9 bits are used for a table lookup
	 *      the highest bit is discarded because it's always set
	 *      the highest nine bits in our example are 100011000
	 *      so we would use the entry 0x18
	 */
	significand = value << (31 - msb);
	logentry = (significand >> 23) & 0xff;
	/**
	 *      last step we do is interpolation because of the
	 *      limitations of the log table the error is that part of
	 *      the significand which isn't used for lookup then we
	 *      compute the ratio between the error and the next table entry
	 *      and interpolate it between the log table entry used and the
	 *      next one the biggest error possible is 0x7fffff
	 *      (in our example it's 0x7D5800)
	 *      needed value for next table entry is 0x800000
	 *      so the interpolation is
	 *      (error / 0x800000) * (logtable_next - logtable_current)
	 *      in the implementation the division is moved to the end for
	 *      better accuracy there is also an overflow correction if
	 *      logtable_next is 256
	 */
	interpolation = ((significand & 0x7fffff) *
		((logtable[(logentry + 1) & 0xff] -
		logtable[logentry]) & 0xffff)) >> 15;

	log2val = ((msb << 24) + (logtable[logentry] << 8) + interpolation);
	/**
	 *      log10(x) = log2(x) * log10(2)
	 */
	log10val = (log2val * LOG10_MAGIC) >> 31;
	/**
	 *      the result is round off to 3 decimal
	 */
	return log10val / ((1 << 24) / 1000);
}

/**
 * computes cross talk suppression sidetone gain.
 *
 * @sig_org: orignal signal level
 * @sig_cros: cross talk signal level
 *
 * The orignal and cross talk signal vlues need to be characterized.
 * Once these values have been characterized, this sidetone value
 * can be converted to decibel with the equation below.
 * sidetone = 20 * log (original signal level / crosstalk signal level)
 *
 * return cross talk sidetone gain
 */
static u32 nau8825_xtalk_sidetone(u32 sig_org, u32 sig_cros)
{
	u32 gain, sidetone;

	if (unlikely(sig_org == 0) || unlikely(sig_cros == 0)) {
		WARN_ON(1);
		return 0;
	}

	sig_org = nau8825_intlog10_dec3(sig_org);
	sig_cros = nau8825_intlog10_dec3(sig_cros);
	if (sig_org >= sig_cros)
		gain = (sig_org - sig_cros) * 20 + GAIN_AUGMENT;
	else
		gain = (sig_cros - sig_org) * 20 + GAIN_AUGMENT;
	sidetone = SIDETONE_BASE - gain * 2;
	sidetone /= 1000;

	return sidetone;
}

static int nau8825_xtalk_baktab_index_by_reg(unsigned int reg)
{
	int index;

	for (index = 0; index < ARRAY_SIZE(nau8825_xtalk_baktab); index++)
		if (nau8825_xtalk_baktab[index].reg == reg)
			return index;
	return -EINVAL;
}

static void nau8825_xtalk_backup(struct nau8825 *nau8825)
{
	int i;

	/* Backup some register values to backup table */
	for (i = 0; i < ARRAY_SIZE(nau8825_xtalk_baktab); i++)
		regmap_read(nau8825->regmap, nau8825_xtalk_baktab[i].reg,
				&nau8825_xtalk_baktab[i].def);
}

static void nau8825_xtalk_restore(struct nau8825 *nau8825)
{
	int i, volume;

	/* Restore register values from backup table; When the driver restores
	 * the headphone volumem, it needs recover to original level gradually
	 * with 3dB per step for less pop noise.
	 */
	for (i = 0; i < ARRAY_SIZE(nau8825_xtalk_baktab); i++) {
		if (nau8825_xtalk_baktab[i].reg == NAU8825_REG_HSVOL_CTRL) {
			/* Ramping up the volume change to reduce pop noise */
			volume = nau8825_xtalk_baktab[i].def &
				NAU8825_HPR_VOL_MASK;
			nau8825_hpvol_ramp(nau8825, 0, volume, 3);
			continue;
		}
		regmap_write(nau8825->regmap, nau8825_xtalk_baktab[i].reg,
				nau8825_xtalk_baktab[i].def);
	}
}

static void nau8825_xtalk_prepare_dac(struct nau8825 *nau8825)
{
	/* Enable power of DAC path */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_DACR | NAU8825_ENABLE_DACL |
		NAU8825_ENABLE_ADC | NAU8825_ENABLE_ADC_CLK |
		NAU8825_ENABLE_DAC_CLK, NAU8825_ENABLE_DACR |
		NAU8825_ENABLE_DACL | NAU8825_ENABLE_ADC |
		NAU8825_ENABLE_ADC_CLK | NAU8825_ENABLE_DAC_CLK);
	/* Prevent startup click by letting charge pump to ramp up and
	 * change bump enable
	 */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
		NAU8825_JAMNODCLOW | NAU8825_CHANRGE_PUMP_EN,
		NAU8825_JAMNODCLOW | NAU8825_CHANRGE_PUMP_EN);
	/* Enable clock sync of DAC and DAC clock */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_RDAC,
		NAU8825_RDAC_EN | NAU8825_RDAC_CLK_EN |
		NAU8825_RDAC_FS_BCLK_ENB,
		NAU8825_RDAC_EN | NAU8825_RDAC_CLK_EN);
	/* Power up output driver with 2 stage */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_POWER_UP_CONTROL,
		NAU8825_POWERUP_INTEGR_R | NAU8825_POWERUP_INTEGR_L |
		NAU8825_POWERUP_DRV_IN_R | NAU8825_POWERUP_DRV_IN_L,
		NAU8825_POWERUP_INTEGR_R | NAU8825_POWERUP_INTEGR_L |
		NAU8825_POWERUP_DRV_IN_R | NAU8825_POWERUP_DRV_IN_L);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_POWER_UP_CONTROL,
		NAU8825_POWERUP_HP_DRV_R | NAU8825_POWERUP_HP_DRV_L,
		NAU8825_POWERUP_HP_DRV_R | NAU8825_POWERUP_HP_DRV_L);
	/* HP outputs not shouted to ground  */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_HSD_CTRL,
		NAU8825_SPKR_DWN1R | NAU8825_SPKR_DWN1L, 0);
	/* Enable HP boost driver */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BOOST,
		NAU8825_HP_BOOST_DIS, NAU8825_HP_BOOST_DIS);
	/* Enable class G compare path to supply 1.8V or 0.9V. */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CLASSG_CTRL,
		NAU8825_CLASSG_LDAC_EN | NAU8825_CLASSG_RDAC_EN,
		NAU8825_CLASSG_LDAC_EN | NAU8825_CLASSG_RDAC_EN);
}

static void nau8825_xtalk_prepare_adc(struct nau8825 *nau8825)
{
	/* Power up left ADC and raise 5dB than Vmid for Vref  */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_ANALOG_ADC_2,
		NAU8825_POWERUP_ADCL | NAU8825_ADC_VREFSEL_MASK,
		NAU8825_POWERUP_ADCL | NAU8825_ADC_VREFSEL_VMID_PLUS_0_5DB);
}

static void nau8825_xtalk_clock(struct nau8825 *nau8825)
{
	/* Recover FLL default value */
	regmap_write(nau8825->regmap, NAU8825_REG_FLL1, 0x0);
	regmap_write(nau8825->regmap, NAU8825_REG_FLL2, 0x3126);
	regmap_write(nau8825->regmap, NAU8825_REG_FLL3, 0x0008);
	regmap_write(nau8825->regmap, NAU8825_REG_FLL4, 0x0010);
	regmap_write(nau8825->regmap, NAU8825_REG_FLL5, 0x0);
	regmap_write(nau8825->regmap, NAU8825_REG_FLL6, 0x6000);
	/* Enable internal VCO clock for detection signal generated */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
		NAU8825_CLK_SRC_MASK, NAU8825_CLK_SRC_VCO);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL6, NAU8825_DCO_EN,
		NAU8825_DCO_EN);
	/* Given specific clock frequency of internal clock to
	 * generate signal.
	 */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
		NAU8825_CLK_MCLK_SRC_MASK, 0xf);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL1,
		NAU8825_FLL_RATIO_MASK, 0x10);
}

static void nau8825_xtalk_prepare(struct nau8825 *nau8825)
{
	int volume, index;

	/* Backup those registers changed by cross talk detection */
	nau8825_xtalk_backup(nau8825);
	/* Config IIS as master to output signal by codec */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK | NAU8825_I2S_LRC_DIV_MASK |
		NAU8825_I2S_BLK_DIV_MASK, NAU8825_I2S_MS_MASTER |
		(0x2 << NAU8825_I2S_LRC_DIV_SFT) | 0x1);
	/* Ramp up headphone volume to 0dB to get better performance and
	 * avoid pop noise in headphone.
	 */
	index = nau8825_xtalk_baktab_index_by_reg(NAU8825_REG_HSVOL_CTRL);
	if (index != -EINVAL) {
		volume = nau8825_xtalk_baktab[index].def &
				NAU8825_HPR_VOL_MASK;
		nau8825_hpvol_ramp(nau8825, volume, 0, 3);
	}
	nau8825_xtalk_clock(nau8825);
	nau8825_xtalk_prepare_dac(nau8825);
	nau8825_xtalk_prepare_adc(nau8825);
	/* Config channel path and digital gain */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_DACL_CTRL,
		NAU8825_DACL_CH_SEL_MASK | NAU8825_DACL_CH_VOL_MASK,
		NAU8825_DACL_CH_SEL_L | 0xab);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_DACR_CTRL,
		NAU8825_DACR_CH_SEL_MASK | NAU8825_DACR_CH_VOL_MASK,
		NAU8825_DACR_CH_SEL_R | 0xab);
	/* Config cross talk parameters and generate the 23Hz sine wave with
	 * 1/16 full scale of signal level for impedance measurement.
	 */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_IMM_MODE_CTRL,
		NAU8825_IMM_THD_MASK | NAU8825_IMM_GEN_VOL_MASK |
		NAU8825_IMM_CYC_MASK | NAU8825_IMM_DAC_SRC_MASK,
		(0x9 << NAU8825_IMM_THD_SFT) | NAU8825_IMM_GEN_VOL_1_16th |
		NAU8825_IMM_CYC_8192 | NAU8825_IMM_DAC_SRC_SIN);
	/* RMS intrruption enable */
	regmap_update_bits(nau8825->regmap,
		NAU8825_REG_INTERRUPT_MASK, NAU8825_IRQ_RMS_EN, 0);
	/* Power up left and right DAC */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
		NAU8825_POWER_DOWN_DACR | NAU8825_POWER_DOWN_DACL, 0);
}

static void nau8825_xtalk_clean_dac(struct nau8825 *nau8825)
{
	/* Disable HP boost driver */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BOOST,
		NAU8825_HP_BOOST_DIS, 0);
	/* HP outputs shouted to ground  */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_HSD_CTRL,
		NAU8825_SPKR_DWN1R | NAU8825_SPKR_DWN1L,
		NAU8825_SPKR_DWN1R | NAU8825_SPKR_DWN1L);
	/* Power down left and right DAC */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
		NAU8825_POWER_DOWN_DACR | NAU8825_POWER_DOWN_DACL,
		NAU8825_POWER_DOWN_DACR | NAU8825_POWER_DOWN_DACL);
	/* Enable the TESTDAC and  disable L/R HP impedance */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_HPR_IMP | NAU8825_BIAS_HPL_IMP |
		NAU8825_BIAS_TESTDAC_EN, NAU8825_BIAS_TESTDAC_EN);
	/* Power down output driver with 2 stage */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_POWER_UP_CONTROL,
		NAU8825_POWERUP_HP_DRV_R | NAU8825_POWERUP_HP_DRV_L, 0);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_POWER_UP_CONTROL,
		NAU8825_POWERUP_INTEGR_R | NAU8825_POWERUP_INTEGR_L |
		NAU8825_POWERUP_DRV_IN_R | NAU8825_POWERUP_DRV_IN_L, 0);
	/* Disable clock sync of DAC and DAC clock */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_RDAC,
		NAU8825_RDAC_EN | NAU8825_RDAC_CLK_EN, 0);
	/* Disable charge pump ramp up function and change bump */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
		NAU8825_JAMNODCLOW | NAU8825_CHANRGE_PUMP_EN, 0);
	/* Disable power of DAC path */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_DACR | NAU8825_ENABLE_DACL |
		NAU8825_ENABLE_ADC_CLK | NAU8825_ENABLE_DAC_CLK, 0);
	if (!nau8825->irq)
		regmap_update_bits(nau8825->regmap,
			NAU8825_REG_ENA_CTRL, NAU8825_ENABLE_ADC, 0);
}

static void nau8825_xtalk_clean_adc(struct nau8825 *nau8825)
{
	/* Power down left ADC and restore voltage to Vmid */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_ANALOG_ADC_2,
		NAU8825_POWERUP_ADCL | NAU8825_ADC_VREFSEL_MASK, 0);
}

static void nau8825_xtalk_clean(struct nau8825 *nau8825)
{
	/* Enable internal VCO needed for interruptions */
	nau8825_configure_sysclk(nau8825, NAU8825_CLK_INTERNAL, 0);
	nau8825_xtalk_clean_dac(nau8825);
	nau8825_xtalk_clean_adc(nau8825);
	/* Clear cross talk parameters and disable */
	regmap_write(nau8825->regmap, NAU8825_REG_IMM_MODE_CTRL, 0);
	/* RMS intrruption disable */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_RMS_EN, NAU8825_IRQ_RMS_EN);
	/* Recover default value for IIS */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK | NAU8825_I2S_LRC_DIV_MASK |
		NAU8825_I2S_BLK_DIV_MASK, NAU8825_I2S_MS_SLAVE);
	/* Restore value of specific register for cross talk */
	nau8825_xtalk_restore(nau8825);
}

static void nau8825_xtalk_imm_start(struct nau8825 *nau8825, int vol)
{
	/* Apply ADC volume for better cross talk performance */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_ADC_DGAIN_CTRL,
				NAU8825_ADC_DIG_VOL_MASK, vol);
	/* Disables JKTIP(HPL) DAC channel for right to left measurement.
	 * Do it before sending signal in order to erase pop noise.
	 */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_TESTDACR_EN | NAU8825_BIAS_TESTDACL_EN,
		NAU8825_BIAS_TESTDACL_EN);
	switch (nau8825->xtalk_state) {
	case NAU8825_XTALK_HPR_R2L:
		/* Enable right headphone impedance */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
			NAU8825_BIAS_HPR_IMP | NAU8825_BIAS_HPL_IMP,
			NAU8825_BIAS_HPR_IMP);
		break;
	case NAU8825_XTALK_HPL_R2L:
		/* Enable left headphone impedance */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
			NAU8825_BIAS_HPR_IMP | NAU8825_BIAS_HPL_IMP,
			NAU8825_BIAS_HPL_IMP);
		break;
	default:
		break;
	}
	msleep(100);
	/* Impedance measurement mode enable */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_IMM_MODE_CTRL,
				NAU8825_IMM_EN, NAU8825_IMM_EN);
}

static void nau8825_xtalk_imm_stop(struct nau8825 *nau8825)
{
	/* Impedance measurement mode disable */
	regmap_update_bits(nau8825->regmap,
		NAU8825_REG_IMM_MODE_CTRL, NAU8825_IMM_EN, 0);
}

/* The cross talk measurement function can reduce cross talk across the
 * JKTIP(HPL) and JKR1(HPR) outputs which measures the cross talk signal
 * level to determine what cross talk reduction gain is. This system works by
 * sending a 23Hz -24dBV sine wave into the headset output DAC and through
 * the PGA. The output of the PGA is then connected to an internal current
 * sense which measures the attenuated 23Hz signal and passing the output to
 * an ADC which converts the measurement to a binary code. With two separated
 * measurement, one for JKR1(HPR) and the other JKTIP(HPL), measurement data
 * can be separated read in IMM_RMS_L for HSR and HSL after each measurement.
 * Thus, the measurement function has four states to complete whole sequence.
 * 1. Prepare state : Prepare the resource for detection and transfer to HPR
 *     IMM stat to make JKR1(HPR) impedance measure.
 * 2. HPR IMM state : Read out orignal signal level of JKR1(HPR) and transfer
 *     to HPL IMM state to make JKTIP(HPL) impedance measure.
 * 3. HPL IMM state : Read out cross talk signal level of JKTIP(HPL) and
 *     transfer to IMM state to determine suppression sidetone gain.
 * 4. IMM state : Computes cross talk suppression sidetone gain with orignal
 *     and cross talk signal level. Apply this gain and then restore codec
 *     configuration. Then transfer to Done state for ending.
 */
static void nau8825_xtalk_measure(struct nau8825 *nau8825)
{
	u32 sidetone;

	switch (nau8825->xtalk_state) {
	case NAU8825_XTALK_PREPARE:
		/* In prepare state, set up clock, intrruption, DAC path, ADC
		 * path and cross talk detection parameters for preparation.
		 */
		nau8825_xtalk_prepare(nau8825);
		msleep(280);
		/* Trigger right headphone impedance detection */
		nau8825->xtalk_state = NAU8825_XTALK_HPR_R2L;
		nau8825_xtalk_imm_start(nau8825, 0x00d2);
		break;
	case NAU8825_XTALK_HPR_R2L:
		/* In right headphone IMM state, read out right headphone
		 * impedance measure result, and then start up left side.
		 */
		regmap_read(nau8825->regmap, NAU8825_REG_IMM_RMS_L,
			&nau8825->imp_rms[NAU8825_XTALK_HPR_R2L]);
		dev_dbg(nau8825->dev, "HPR_R2L imm: %x\n",
			nau8825->imp_rms[NAU8825_XTALK_HPR_R2L]);
		/* Disable then re-enable IMM mode to update */
		nau8825_xtalk_imm_stop(nau8825);
		/* Trigger left headphone impedance detection */
		nau8825->xtalk_state = NAU8825_XTALK_HPL_R2L;
		nau8825_xtalk_imm_start(nau8825, 0x00ff);
		break;
	case NAU8825_XTALK_HPL_R2L:
		/* In left headphone IMM state, read out left headphone
		 * impedance measure result, and delay some time to wait
		 * detection sine wave output finish. Then, we can calculate
		 * the cross talk suppresstion side tone according to the L/R
		 * headphone imedance.
		 */
		regmap_read(nau8825->regmap, NAU8825_REG_IMM_RMS_L,
			&nau8825->imp_rms[NAU8825_XTALK_HPL_R2L]);
		dev_dbg(nau8825->dev, "HPL_R2L imm: %x\n",
			nau8825->imp_rms[NAU8825_XTALK_HPL_R2L]);
		nau8825_xtalk_imm_stop(nau8825);
		msleep(150);
		nau8825->xtalk_state = NAU8825_XTALK_IMM;
		break;
	case NAU8825_XTALK_IMM:
		/* In impedance measure state, the orignal and cross talk
		 * signal level vlues are ready. The side tone gain is deter-
		 * mined with these signal level. After all, restore codec
		 * configuration.
		 */
		sidetone = nau8825_xtalk_sidetone(
			nau8825->imp_rms[NAU8825_XTALK_HPR_R2L],
			nau8825->imp_rms[NAU8825_XTALK_HPL_R2L]);
		dev_dbg(nau8825->dev, "cross talk sidetone: %x\n", sidetone);
		regmap_write(nau8825->regmap, NAU8825_REG_DAC_DGAIN_CTRL,
					(sidetone << 8) | sidetone);
		nau8825_xtalk_clean(nau8825);
		nau8825->xtalk_state = NAU8825_XTALK_DONE;
		break;
	default:
		break;
	}
}

static void nau8825_xtalk_work(struct work_struct *work)
{
	struct nau8825 *nau8825 = container_of(
		work, struct nau8825, xtalk_work);

	nau8825_xtalk_measure(nau8825);
	/* To determine the cross talk side tone gain when reach
	 * the impedance measure state.
	 */
	if (nau8825->xtalk_state == NAU8825_XTALK_IMM)
		nau8825_xtalk_measure(nau8825);

	/* Delay jack report until cross talk detection process
	 * completed. It can avoid application to do playback
	 * preparation before cross talk detection is still working.
	 * Meanwhile, the protection of the cross talk detection
	 * is released.
	 */
	if (nau8825->xtalk_state == NAU8825_XTALK_DONE) {
		snd_soc_jack_report(nau8825->jack, nau8825->xtalk_event,
				nau8825->xtalk_event_mask);
		nau8825_sema_release(nau8825);
		nau8825->xtalk_protect = false;
	}
}

static void nau8825_xtalk_cancel(struct nau8825 *nau8825)
{
	/* If the crosstalk is eanbled and the process is on going,
	 * the driver forces to cancel the crosstalk task and
	 * restores the configuration to original status.
	 */
	if (nau8825->xtalk_enable && nau8825->xtalk_state !=
		NAU8825_XTALK_DONE) {
		cancel_work_sync(&nau8825->xtalk_work);
		nau8825_xtalk_clean(nau8825);
	}
	/* Reset parameters for cross talk suppression function */
	nau8825_sema_reset(nau8825);
	nau8825->xtalk_state = NAU8825_XTALK_DONE;
	nau8825->xtalk_protect = false;
}

static bool nau8825_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8825_REG_ENA_CTRL ... NAU8825_REG_FLL_VCO_RSV:
	case NAU8825_REG_HSD_CTRL ... NAU8825_REG_JACK_DET_CTRL:
	case NAU8825_REG_INTERRUPT_MASK ... NAU8825_REG_KEYDET_CTRL:
	case NAU8825_REG_VDET_THRESHOLD_1 ... NAU8825_REG_DACR_CTRL:
	case NAU8825_REG_ADC_DRC_KNEE_IP12 ... NAU8825_REG_ADC_DRC_ATKDCY:
	case NAU8825_REG_DAC_DRC_KNEE_IP12 ... NAU8825_REG_DAC_DRC_ATKDCY:
	case NAU8825_REG_IMM_MODE_CTRL ... NAU8825_REG_IMM_RMS_R:
	case NAU8825_REG_CLASSG_CTRL ... NAU8825_REG_OPT_EFUSE_CTRL:
	case NAU8825_REG_MISC_CTRL:
	case NAU8825_REG_I2C_DEVICE_ID ... NAU8825_REG_SARDOUT_RAM_STATUS:
	case NAU8825_REG_BIAS_ADJ:
	case NAU8825_REG_TRIM_SETTINGS ... NAU8825_REG_ANALOG_CONTROL_2:
	case NAU8825_REG_ANALOG_ADC_1 ... NAU8825_REG_MIC_BIAS:
	case NAU8825_REG_BOOST ... NAU8825_REG_FEPGA:
	case NAU8825_REG_POWER_UP_CONTROL ... NAU8825_REG_GENERAL_STATUS:
		return true;
	default:
		return false;
	}

}

static bool nau8825_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8825_REG_RESET ... NAU8825_REG_FLL_VCO_RSV:
	case NAU8825_REG_HSD_CTRL ... NAU8825_REG_JACK_DET_CTRL:
	case NAU8825_REG_INTERRUPT_MASK:
	case NAU8825_REG_INT_CLR_KEY_STATUS ... NAU8825_REG_KEYDET_CTRL:
	case NAU8825_REG_VDET_THRESHOLD_1 ... NAU8825_REG_DACR_CTRL:
	case NAU8825_REG_ADC_DRC_KNEE_IP12 ... NAU8825_REG_ADC_DRC_ATKDCY:
	case NAU8825_REG_DAC_DRC_KNEE_IP12 ... NAU8825_REG_DAC_DRC_ATKDCY:
	case NAU8825_REG_IMM_MODE_CTRL:
	case NAU8825_REG_CLASSG_CTRL ... NAU8825_REG_OPT_EFUSE_CTRL:
	case NAU8825_REG_MISC_CTRL:
	case NAU8825_REG_BIAS_ADJ:
	case NAU8825_REG_TRIM_SETTINGS ... NAU8825_REG_ANALOG_CONTROL_2:
	case NAU8825_REG_ANALOG_ADC_1 ... NAU8825_REG_MIC_BIAS:
	case NAU8825_REG_BOOST ... NAU8825_REG_FEPGA:
	case NAU8825_REG_POWER_UP_CONTROL ... NAU8825_REG_CHARGE_PUMP:
		return true;
	default:
		return false;
	}
}

static bool nau8825_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8825_REG_RESET:
	case NAU8825_REG_IRQ_STATUS:
	case NAU8825_REG_INT_CLR_KEY_STATUS:
	case NAU8825_REG_IMM_RMS_L:
	case NAU8825_REG_IMM_RMS_R:
	case NAU8825_REG_I2C_DEVICE_ID:
	case NAU8825_REG_SARDOUT_RAM_STATUS:
	case NAU8825_REG_CHARGE_PUMP_INPUT_READ:
	case NAU8825_REG_GENERAL_STATUS:
	case NAU8825_REG_BIQ_CTRL ... NAU8825_REG_BIQ_COF10:
		return true;
	default:
		return false;
	}
}

static int nau8825_adc_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(nau8825->regmap, NAU8825_REG_ENA_CTRL,
			NAU8825_ENABLE_ADC, NAU8825_ENABLE_ADC);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (!nau8825->irq)
			regmap_update_bits(nau8825->regmap,
				NAU8825_REG_ENA_CTRL, NAU8825_ENABLE_ADC, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8825_pump_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Prevent startup click by letting charge pump to ramp up */
		msleep(10);
		regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
			NAU8825_JAMNODCLOW, NAU8825_JAMNODCLOW);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
			NAU8825_JAMNODCLOW, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8825_output_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Disables the TESTDAC to let DAC signal pass through. */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
			NAU8825_BIAS_TESTDAC_EN, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
			NAU8825_BIAS_TESTDAC_EN, NAU8825_BIAS_TESTDAC_EN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8825_biq_coeff_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;

	if (!component->regmap)
		return -EINVAL;

	regmap_raw_read(component->regmap, NAU8825_REG_BIQ_COF1,
		ucontrol->value.bytes.data, params->max);
	return 0;
}

static int nau8825_biq_coeff_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	void *data;

	if (!component->regmap)
		return -EINVAL;

	data = kmemdup(ucontrol->value.bytes.data,
		params->max, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	regmap_update_bits(component->regmap, NAU8825_REG_BIQ_CTRL,
		NAU8825_BIQ_WRT_EN, 0);
	regmap_raw_write(component->regmap, NAU8825_REG_BIQ_COF1,
		data, params->max);
	regmap_update_bits(component->regmap, NAU8825_REG_BIQ_CTRL,
		NAU8825_BIQ_WRT_EN, NAU8825_BIQ_WRT_EN);

	kfree(data);
	return 0;
}

static const char * const nau8825_biq_path[] = {
	"ADC", "DAC"
};

static const struct soc_enum nau8825_biq_path_enum =
	SOC_ENUM_SINGLE(NAU8825_REG_BIQ_CTRL, NAU8825_BIQ_PATH_SFT,
		ARRAY_SIZE(nau8825_biq_path), nau8825_biq_path);

static const char * const nau8825_adc_decimation[] = {
	"32", "64", "128", "256"
};

static const struct soc_enum nau8825_adc_decimation_enum =
	SOC_ENUM_SINGLE(NAU8825_REG_ADC_RATE, NAU8825_ADC_SYNC_DOWN_SFT,
		ARRAY_SIZE(nau8825_adc_decimation), nau8825_adc_decimation);

static const char * const nau8825_dac_oversampl[] = {
	"64", "256", "128", "", "32"
};

static const struct soc_enum nau8825_dac_oversampl_enum =
	SOC_ENUM_SINGLE(NAU8825_REG_DAC_CTRL1, NAU8825_DAC_OVERSAMPLE_SFT,
		ARRAY_SIZE(nau8825_dac_oversampl), nau8825_dac_oversampl);

static const DECLARE_TLV_DB_MINMAX_MUTE(adc_vol_tlv, -10300, 2400);
static const DECLARE_TLV_DB_MINMAX_MUTE(sidetone_vol_tlv, -4200, 0);
static const DECLARE_TLV_DB_MINMAX(dac_vol_tlv, -5400, 0);
static const DECLARE_TLV_DB_MINMAX(fepga_gain_tlv, -100, 3600);
static const DECLARE_TLV_DB_MINMAX_MUTE(crosstalk_vol_tlv, -9600, 2400);

static const struct snd_kcontrol_new nau8825_controls[] = {
	SOC_SINGLE_TLV("Mic Volume", NAU8825_REG_ADC_DGAIN_CTRL,
		0, 0xff, 0, adc_vol_tlv),
	SOC_DOUBLE_TLV("Headphone Bypass Volume", NAU8825_REG_ADC_DGAIN_CTRL,
		12, 8, 0x0f, 0, sidetone_vol_tlv),
	SOC_DOUBLE_TLV("Headphone Volume", NAU8825_REG_HSVOL_CTRL,
		6, 0, 0x3f, 1, dac_vol_tlv),
	SOC_SINGLE_TLV("Frontend PGA Volume", NAU8825_REG_POWER_UP_CONTROL,
		8, 37, 0, fepga_gain_tlv),
	SOC_DOUBLE_TLV("Headphone Crosstalk Volume", NAU8825_REG_DAC_DGAIN_CTRL,
		0, 8, 0xff, 0, crosstalk_vol_tlv),

	SOC_ENUM("ADC Decimation Rate", nau8825_adc_decimation_enum),
	SOC_ENUM("DAC Oversampling Rate", nau8825_dac_oversampl_enum),
	/* programmable biquad filter */
	SOC_ENUM("BIQ Path Select", nau8825_biq_path_enum),
	SND_SOC_BYTES_EXT("BIQ Coefficients", 20,
		  nau8825_biq_coeff_get, nau8825_biq_coeff_put),
};

/* DAC Mux 0x33[9] and 0x34[9] */
static const char * const nau8825_dac_src[] = {
	"DACL", "DACR",
};

static SOC_ENUM_SINGLE_DECL(
	nau8825_dacl_enum, NAU8825_REG_DACL_CTRL,
	NAU8825_DACL_CH_SEL_SFT, nau8825_dac_src);

static SOC_ENUM_SINGLE_DECL(
	nau8825_dacr_enum, NAU8825_REG_DACR_CTRL,
	NAU8825_DACR_CH_SEL_SFT, nau8825_dac_src);

static const struct snd_kcontrol_new nau8825_dacl_mux =
	SOC_DAPM_ENUM("DACL Source", nau8825_dacl_enum);

static const struct snd_kcontrol_new nau8825_dacr_mux =
	SOC_DAPM_ENUM("DACR Source", nau8825_dacr_enum);


static const struct snd_soc_dapm_widget nau8825_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("AIFTX", "Capture", 0, NAU8825_REG_I2S_PCM_CTRL2,
		15, 1),

	SND_SOC_DAPM_INPUT("MIC"),
	SND_SOC_DAPM_MICBIAS("MICBIAS", NAU8825_REG_MIC_BIAS, 8, 0),

	SND_SOC_DAPM_PGA("Frontend PGA", NAU8825_REG_POWER_UP_CONTROL, 14, 0,
		NULL, 0),

	SND_SOC_DAPM_ADC_E("ADC", NULL, SND_SOC_NOPM, 0, 0,
		nau8825_adc_event, SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("ADC Clock", NAU8825_REG_ENA_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Power", NAU8825_REG_ANALOG_ADC_2, 6, 0, NULL,
		0),

	/* ADC for button press detection. A dapm supply widget is used to
	 * prevent dapm_power_widgets keeping the codec at SND_SOC_BIAS_ON
	 * during suspend.
	 */
	SND_SOC_DAPM_SUPPLY("SAR", NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_ADC_EN_SFT, 0, NULL, 0),

	SND_SOC_DAPM_PGA_S("ADACL", 2, NAU8825_REG_RDAC, 12, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACR", 2, NAU8825_REG_RDAC, 13, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACL Clock", 3, NAU8825_REG_RDAC, 8, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACR Clock", 3, NAU8825_REG_RDAC, 9, 0, NULL, 0),

	SND_SOC_DAPM_DAC("DDACR", NULL, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_DACR_SFT, 0),
	SND_SOC_DAPM_DAC("DDACL", NULL, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_DACL_SFT, 0),
	SND_SOC_DAPM_SUPPLY("DDAC Clock", NAU8825_REG_ENA_CTRL, 6, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DACL Mux", SND_SOC_NOPM, 0, 0, &nau8825_dacl_mux),
	SND_SOC_DAPM_MUX("DACR Mux", SND_SOC_NOPM, 0, 0, &nau8825_dacr_mux),

	SND_SOC_DAPM_PGA_S("HP amp L", 0,
		NAU8825_REG_CLASSG_CTRL, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("HP amp R", 0,
		NAU8825_REG_CLASSG_CTRL, 2, 0, NULL, 0),

	SND_SOC_DAPM_PGA_S("Charge Pump", 1, NAU8825_REG_CHARGE_PUMP, 5, 0,
		nau8825_pump_event, SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_S("Output Driver R Stage 1", 4,
		NAU8825_REG_POWER_UP_CONTROL, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 1", 4,
		NAU8825_REG_POWER_UP_CONTROL, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver R Stage 2", 5,
		NAU8825_REG_POWER_UP_CONTROL, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 2", 5,
		NAU8825_REG_POWER_UP_CONTROL, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver R Stage 3", 6,
		NAU8825_REG_POWER_UP_CONTROL, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 3", 6,
		NAU8825_REG_POWER_UP_CONTROL, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA_S("Output DACL", 7,
		NAU8825_REG_CHARGE_PUMP, 8, 1, nau8825_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_S("Output DACR", 7,
		NAU8825_REG_CHARGE_PUMP, 9, 1, nau8825_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* HPOL/R are ungrounded by disabling 16 Ohm pull-downs on playback */
	SND_SOC_DAPM_PGA_S("HPOL Pulldown", 8,
		NAU8825_REG_HSD_CTRL, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA_S("HPOR Pulldown", 8,
		NAU8825_REG_HSD_CTRL, 1, 1, NULL, 0),

	/* High current HPOL/R boost driver */
	SND_SOC_DAPM_PGA_S("HP Boost Driver", 9,
		NAU8825_REG_BOOST, 9, 1, NULL, 0),

	/* Class G operation control*/
	SND_SOC_DAPM_PGA_S("Class G", 10,
		NAU8825_REG_CLASSG_CTRL, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route nau8825_dapm_routes[] = {
	{"Frontend PGA", NULL, "MIC"},
	{"ADC", NULL, "Frontend PGA"},
	{"ADC", NULL, "ADC Clock"},
	{"ADC", NULL, "ADC Power"},
	{"AIFTX", NULL, "ADC"},

	{"DDACL", NULL, "Playback"},
	{"DDACR", NULL, "Playback"},
	{"DDACL", NULL, "DDAC Clock"},
	{"DDACR", NULL, "DDAC Clock"},
	{"DACL Mux", "DACL", "DDACL"},
	{"DACL Mux", "DACR", "DDACR"},
	{"DACR Mux", "DACL", "DDACL"},
	{"DACR Mux", "DACR", "DDACR"},
	{"HP amp L", NULL, "DACL Mux"},
	{"HP amp R", NULL, "DACR Mux"},
	{"Charge Pump", NULL, "HP amp L"},
	{"Charge Pump", NULL, "HP amp R"},
	{"ADACL", NULL, "Charge Pump"},
	{"ADACR", NULL, "Charge Pump"},
	{"ADACL Clock", NULL, "ADACL"},
	{"ADACR Clock", NULL, "ADACR"},
	{"Output Driver L Stage 1", NULL, "ADACL Clock"},
	{"Output Driver R Stage 1", NULL, "ADACR Clock"},
	{"Output Driver L Stage 2", NULL, "Output Driver L Stage 1"},
	{"Output Driver R Stage 2", NULL, "Output Driver R Stage 1"},
	{"Output Driver L Stage 3", NULL, "Output Driver L Stage 2"},
	{"Output Driver R Stage 3", NULL, "Output Driver R Stage 2"},
	{"Output DACL", NULL, "Output Driver L Stage 3"},
	{"Output DACR", NULL, "Output Driver R Stage 3"},
	{"HPOL Pulldown", NULL, "Output DACL"},
	{"HPOR Pulldown", NULL, "Output DACR"},
	{"HP Boost Driver", NULL, "HPOL Pulldown"},
	{"HP Boost Driver", NULL, "HPOR Pulldown"},
	{"Class G", NULL, "HP Boost Driver"},
	{"HPOL", NULL, "Class G"},
	{"HPOR", NULL, "Class G"},
};

static int nau8825_clock_check(struct nau8825 *nau8825,
	int stream, int rate, int osr)
{
	int osrate;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (osr >= ARRAY_SIZE(osr_dac_sel))
			return -EINVAL;
		osrate = osr_dac_sel[osr].osr;
	} else {
		if (osr >= ARRAY_SIZE(osr_adc_sel))
			return -EINVAL;
		osrate = osr_adc_sel[osr].osr;
	}

	if (!osrate || rate * osr > CLK_DA_AD_MAX) {
		dev_err(nau8825->dev, "exceed the maximum frequency of CLK_ADC or CLK_DAC\n");
		return -EINVAL;
	}

	return 0;
}

static int nau8825_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, osr, ctrl_val, bclk_fs, bclk_div;

	nau8825_sema_acquire(nau8825, 3 * HZ);

	/* CLK_DAC or CLK_ADC = OSR * FS
	 * DAC or ADC clock frequency is defined as Over Sampling Rate (OSR)
	 * multiplied by the audio sample rate (Fs). Note that the OSR and Fs
	 * values must be selected such that the maximum frequency is less
	 * than 6.144 MHz.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_read(nau8825->regmap, NAU8825_REG_DAC_CTRL1, &osr);
		osr &= NAU8825_DAC_OVERSAMPLE_MASK;
		if (nau8825_clock_check(nau8825, substream->stream,
			params_rate(params), osr))
			return -EINVAL;
		regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
			NAU8825_CLK_DAC_SRC_MASK,
			osr_dac_sel[osr].clk_src << NAU8825_CLK_DAC_SRC_SFT);
	} else {
		regmap_read(nau8825->regmap, NAU8825_REG_ADC_RATE, &osr);
		osr &= NAU8825_ADC_SYNC_DOWN_MASK;
		if (nau8825_clock_check(nau8825, substream->stream,
			params_rate(params), osr))
			return -EINVAL;
		regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
			NAU8825_CLK_ADC_SRC_MASK,
			osr_adc_sel[osr].clk_src << NAU8825_CLK_ADC_SRC_SFT);
	}

	/* make BCLK and LRC divde configuration if the codec as master. */
	regmap_read(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL2, &ctrl_val);
	if (ctrl_val & NAU8825_I2S_MS_MASTER) {
		/* get the bclk and fs ratio */
		bclk_fs = snd_soc_params_to_bclk(params) / params_rate(params);
		if (bclk_fs <= 32)
			bclk_div = 2;
		else if (bclk_fs <= 64)
			bclk_div = 1;
		else if (bclk_fs <= 128)
			bclk_div = 0;
		else
			return -EINVAL;
		regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL2,
			NAU8825_I2S_LRC_DIV_MASK | NAU8825_I2S_BLK_DIV_MASK,
			((bclk_div + 1) << NAU8825_I2S_LRC_DIV_SFT) | bclk_div);
	}

	switch (params_width(params)) {
	case 16:
		val_len |= NAU8825_I2S_DL_16;
		break;
	case 20:
		val_len |= NAU8825_I2S_DL_20;
		break;
	case 24:
		val_len |= NAU8825_I2S_DL_24;
		break;
	case 32:
		val_len |= NAU8825_I2S_DL_32;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL1,
		NAU8825_I2S_DL_MASK, val_len);

	/* Release the semaphore. */
	nau8825_sema_release(nau8825);

	return 0;
}

static int nau8825_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	unsigned int ctrl1_val = 0, ctrl2_val = 0;

	nau8825_sema_acquire(nau8825, 3 * HZ);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl2_val |= NAU8825_I2S_MS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl1_val |= NAU8825_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl1_val |= NAU8825_I2S_DF_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl1_val |= NAU8825_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl1_val |= NAU8825_I2S_DF_RIGTH;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ctrl1_val |= NAU8825_I2S_DF_PCM_AB;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ctrl1_val |= NAU8825_I2S_DF_PCM_AB;
		ctrl1_val |= NAU8825_I2S_PCMB_EN;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL1,
		NAU8825_I2S_DL_MASK | NAU8825_I2S_DF_MASK |
		NAU8825_I2S_BP_MASK | NAU8825_I2S_PCMB_MASK,
		ctrl1_val);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK, ctrl2_val);

	/* Release the semaphore. */
	nau8825_sema_release(nau8825);

	return 0;
}

static const struct snd_soc_dai_ops nau8825_dai_ops = {
	.hw_params	= nau8825_hw_params,
	.set_fmt	= nau8825_set_dai_fmt,
};

#define NAU8825_RATES	SNDRV_PCM_RATE_8000_192000
#define NAU8825_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
			 | SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver nau8825_dai = {
	.name = "nau8825-hifi",
	.playback = {
		.stream_name	 = "Playback",
		.channels_min	 = 1,
		.channels_max	 = 2,
		.rates		 = NAU8825_RATES,
		.formats	 = NAU8825_FORMATS,
	},
	.capture = {
		.stream_name	 = "Capture",
		.channels_min	 = 1,
		.channels_max	 = 1,
		.rates		 = NAU8825_RATES,
		.formats	 = NAU8825_FORMATS,
	},
	.ops = &nau8825_dai_ops,
};

/**
 * nau8825_enable_jack_detect - Specify a jack for event reporting
 *
 * @component:  component to register the jack with
 * @jack: jack to use to report headset and button events on
 *
 * After this function has been called the headset insert/remove and button
 * events will be routed to the given jack.  Jack can be null to stop
 * reporting.
 */
int nau8825_enable_jack_detect(struct snd_soc_codec *codec,
				struct snd_soc_jack *jack)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	struct regmap *regmap = nau8825->regmap;

	nau8825->jack = jack;

	/* Ground HP Outputs[1:0], needed for headset auto detection
	 * Enable Automatic Mic/Gnd switching reading on insert interrupt[6]
	 */
	regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL,
		NAU8825_HSD_AUTO_MODE | NAU8825_SPKR_DWN1R | NAU8825_SPKR_DWN1L,
		NAU8825_HSD_AUTO_MODE | NAU8825_SPKR_DWN1R | NAU8825_SPKR_DWN1L);

	return 0;
}
EXPORT_SYMBOL_GPL(nau8825_enable_jack_detect);


static bool nau8825_is_jack_inserted(struct regmap *regmap)
{
	bool active_high, is_high;
	int status, jkdet;

	regmap_read(regmap, NAU8825_REG_JACK_DET_CTRL, &jkdet);
	active_high = jkdet & NAU8825_JACK_POLARITY;
	regmap_read(regmap, NAU8825_REG_I2C_DEVICE_ID, &status);
	is_high = status & NAU8825_GPIO2JD1;
	/* return jack connection status according to jack insertion logic
	 * active high or active low.
	 */
	return active_high == is_high;
}

static void nau8825_restart_jack_detection(struct regmap *regmap)
{
	/* this will restart the entire jack detection process including MIC/GND
	 * switching and create interrupts. We have to go from 0 to 1 and back
	 * to 0 to restart.
	 */
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_DET_RESTART, NAU8825_JACK_DET_RESTART);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_DET_RESTART, 0);
}

static void nau8825_int_status_clear_all(struct regmap *regmap)
{
	int active_irq, clear_irq, i;

	/* Reset the intrruption status from rightmost bit if the corres-
	 * ponding irq event occurs.
	 */
	regmap_read(regmap, NAU8825_REG_IRQ_STATUS, &active_irq);
	for (i = 0; i < NAU8825_REG_DATA_LEN; i++) {
		clear_irq = (0x1 << i);
		if (active_irq & clear_irq)
			regmap_write(regmap,
				NAU8825_REG_INT_CLR_KEY_STATUS, clear_irq);
	}
}

static void nau8825_eject_jack(struct nau8825 *nau8825)
{
	struct snd_soc_dapm_context *dapm = nau8825->dapm;
	struct regmap *regmap = nau8825->regmap;

	/* Force to cancel the cross talk detection process */
	nau8825_xtalk_cancel(nau8825);

	snd_soc_dapm_disable_pin(dapm, "SAR");
	snd_soc_dapm_disable_pin(dapm, "MICBIAS");
	/* Detach 2kOhm Resistors from MICBIAS to MICGND1/2 */
	regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
		NAU8825_MICBIAS_JKSLV | NAU8825_MICBIAS_JKR2, 0);
	/* ground HPL/HPR, MICGRND1/2 */
	regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL, 0xf, 0xf);

	snd_soc_dapm_sync(dapm);

	/* Clear all interruption status */
	nau8825_int_status_clear_all(regmap);

	/* Enable the insertion interruption, disable the ejection inter-
	 * ruption, and then bypass de-bounce circuit.
	 */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_DIS_CTRL,
		NAU8825_IRQ_EJECT_DIS | NAU8825_IRQ_INSERT_DIS,
		NAU8825_IRQ_EJECT_DIS);
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_OUTPUT_EN | NAU8825_IRQ_EJECT_EN |
		NAU8825_IRQ_HEADSET_COMPLETE_EN | NAU8825_IRQ_INSERT_EN,
		NAU8825_IRQ_OUTPUT_EN | NAU8825_IRQ_EJECT_EN |
		NAU8825_IRQ_HEADSET_COMPLETE_EN);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_DET_DB_BYPASS, NAU8825_JACK_DET_DB_BYPASS);

	/* Disable ADC needed for interruptions at audo mode */
	regmap_update_bits(regmap, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_ADC, 0);

	/* Close clock for jack type detection at manual mode */
	nau8825_configure_sysclk(nau8825, NAU8825_CLK_DIS, 0);
}

/* Enable audo mode interruptions with internal clock. */
static void nau8825_setup_auto_irq(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;

	/* Enable headset jack type detection complete interruption and
	 * jack ejection interruption.
	 */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_HEADSET_COMPLETE_EN | NAU8825_IRQ_EJECT_EN, 0);

	/* Enable internal VCO needed for interruptions */
	nau8825_configure_sysclk(nau8825, NAU8825_CLK_INTERNAL, 0);

	/* Enable ADC needed for interruptions */
	regmap_update_bits(regmap, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_ADC, NAU8825_ENABLE_ADC);

	/* Chip needs one FSCLK cycle in order to generate interruptions,
	 * as we cannot guarantee one will be provided by the system. Turning
	 * master mode on then off enables us to generate that FSCLK cycle
	 * with a minimum of contention on the clock bus.
	 */
	regmap_update_bits(regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK, NAU8825_I2S_MS_MASTER);
	regmap_update_bits(regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK, NAU8825_I2S_MS_SLAVE);

	/* Not bypass de-bounce circuit */
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_DET_DB_BYPASS, 0);

	/* Unmask all interruptions */
	regmap_write(regmap, NAU8825_REG_INTERRUPT_DIS_CTRL, 0);

	/* Restart the jack detection process at auto mode */
	nau8825_restart_jack_detection(regmap);
}

static int nau8825_button_decode(int value)
{
	int buttons = 0;

	/* The chip supports up to 8 buttons, but ALSA defines only 6 buttons */
	if (value & BIT(0))
		buttons |= SND_JACK_BTN_0;
	if (value & BIT(1))
		buttons |= SND_JACK_BTN_1;
	if (value & BIT(2))
		buttons |= SND_JACK_BTN_2;
	if (value & BIT(3))
		buttons |= SND_JACK_BTN_3;
	if (value & BIT(4))
		buttons |= SND_JACK_BTN_4;
	if (value & BIT(5))
		buttons |= SND_JACK_BTN_5;

	return buttons;
}

static int nau8825_jack_insert(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;
	struct snd_soc_dapm_context *dapm = nau8825->dapm;
	int jack_status_reg, mic_detected;
	int type = 0;

	regmap_read(regmap, NAU8825_REG_GENERAL_STATUS, &jack_status_reg);
	mic_detected = (jack_status_reg >> 10) & 3;
	/* The JKSLV and JKR2 all detected in high impedance headset */
	if (mic_detected == 0x3)
		nau8825->high_imped = true;
	else
		nau8825->high_imped = false;

	switch (mic_detected) {
	case 0:
		/* no mic */
		type = SND_JACK_HEADPHONE;
		break;
	case 1:
		dev_dbg(nau8825->dev, "OMTP (micgnd1) mic connected\n");
		type = SND_JACK_HEADSET;

		/* Unground MICGND1 */
		regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL, 3 << 2,
			1 << 2);
		/* Attach 2kOhm Resistor from MICBIAS to MICGND1 */
		regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
			NAU8825_MICBIAS_JKSLV | NAU8825_MICBIAS_JKR2,
			NAU8825_MICBIAS_JKR2);
		/* Attach SARADC to MICGND1 */
		regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
			NAU8825_SAR_INPUT_MASK,
			NAU8825_SAR_INPUT_JKR2);

		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
		snd_soc_dapm_force_enable_pin(dapm, "SAR");
		snd_soc_dapm_sync(dapm);
		break;
	case 2:
		dev_dbg(nau8825->dev, "CTIA (micgnd2) mic connected\n");
		type = SND_JACK_HEADSET;

		/* Unground MICGND2 */
		regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL, 3 << 2,
			2 << 2);
		/* Attach 2kOhm Resistor from MICBIAS to MICGND2 */
		regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
			NAU8825_MICBIAS_JKSLV | NAU8825_MICBIAS_JKR2,
			NAU8825_MICBIAS_JKSLV);
		/* Attach SARADC to MICGND2 */
		regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
			NAU8825_SAR_INPUT_MASK,
			NAU8825_SAR_INPUT_JKSLV);

		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
		snd_soc_dapm_force_enable_pin(dapm, "SAR");
		snd_soc_dapm_sync(dapm);
		break;
	case 3:
		/* detect error case */
		dev_err(nau8825->dev, "detection error; disable mic function\n");
		type = SND_JACK_HEADPHONE;
		break;
	}

	/* Leaving HPOL/R grounded after jack insert by default. They will be
	 * ungrounded as part of the widget power up sequence at the beginning
	 * of playback to reduce pop.
	 */
	return type;
}

#define NAU8825_BUTTONS (SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
		SND_JACK_BTN_2 | SND_JACK_BTN_3)

static irqreturn_t nau8825_interrupt(int irq, void *data)
{
	struct nau8825 *nau8825 = (struct nau8825 *)data;
	struct regmap *regmap = nau8825->regmap;
	int active_irq, clear_irq = 0, event = 0, event_mask = 0;

	if (regmap_read(regmap, NAU8825_REG_IRQ_STATUS, &active_irq)) {
		dev_err(nau8825->dev, "failed to read irq status\n");
		return IRQ_NONE;
	}

	if ((active_irq & NAU8825_JACK_EJECTION_IRQ_MASK) ==
		NAU8825_JACK_EJECTION_DETECTED) {

		nau8825_eject_jack(nau8825);
		event_mask |= SND_JACK_HEADSET;
		clear_irq = NAU8825_JACK_EJECTION_IRQ_MASK;
	} else if (active_irq & NAU8825_KEY_SHORT_PRESS_IRQ) {
		int key_status;

		regmap_read(regmap, NAU8825_REG_INT_CLR_KEY_STATUS,
			&key_status);

		/* upper 8 bits of the register are for short pressed keys,
		 * lower 8 bits - for long pressed buttons
		 */
		nau8825->button_pressed = nau8825_button_decode(
			key_status >> 8);

		event |= nau8825->button_pressed;
		event_mask |= NAU8825_BUTTONS;
		clear_irq = NAU8825_KEY_SHORT_PRESS_IRQ;
	} else if (active_irq & NAU8825_KEY_RELEASE_IRQ) {
		event_mask = NAU8825_BUTTONS;
		clear_irq = NAU8825_KEY_RELEASE_IRQ;
	} else if (active_irq & NAU8825_HEADSET_COMPLETION_IRQ) {
		if (nau8825_is_jack_inserted(regmap)) {
			event |= nau8825_jack_insert(nau8825);
			if (nau8825->xtalk_enable && !nau8825->high_imped) {
				/* Apply the cross talk suppression in the
				 * headset without high impedance.
				 */
				if (!nau8825->xtalk_protect) {
					/* Raise protection for cross talk de-
					 * tection if no protection before.
					 * The driver has to cancel the pro-
					 * cess and restore changes if process
					 * is ongoing when ejection.
					 */
					int ret;
					nau8825->xtalk_protect = true;
					ret = nau8825_sema_acquire(nau8825, 0);
					if (ret < 0)
						nau8825->xtalk_protect = false;
				}
				/* Startup cross talk detection process */
				nau8825->xtalk_state = NAU8825_XTALK_PREPARE;
				schedule_work(&nau8825->xtalk_work);
			} else {
				/* The cross talk suppression shouldn't apply
				 * in the headset with high impedance. Thus,
				 * relieve the protection raised before.
				 */
				if (nau8825->xtalk_protect) {
					nau8825_sema_release(nau8825);
					nau8825->xtalk_protect = false;
				}
			}
		} else {
			dev_warn(nau8825->dev, "Headset completion IRQ fired but no headset connected\n");
			nau8825_eject_jack(nau8825);
		}

		event_mask |= SND_JACK_HEADSET;
		clear_irq = NAU8825_HEADSET_COMPLETION_IRQ;
		/* Record the interruption report event for driver to report
		 * the event later. The jack report will delay until cross
		 * talk detection process is done.
		 */
		if (nau8825->xtalk_state == NAU8825_XTALK_PREPARE) {
			nau8825->xtalk_event = event;
			nau8825->xtalk_event_mask = event_mask;
		}
	} else if (active_irq & NAU8825_IMPEDANCE_MEAS_IRQ) {
		if (nau8825->xtalk_enable)
			schedule_work(&nau8825->xtalk_work);
		clear_irq = NAU8825_IMPEDANCE_MEAS_IRQ;
	} else if ((active_irq & NAU8825_JACK_INSERTION_IRQ_MASK) ==
		NAU8825_JACK_INSERTION_DETECTED) {
		/* One more step to check GPIO status directly. Thus, the
		 * driver can confirm the real insertion interruption because
		 * the intrruption at manual mode has bypassed debounce
		 * circuit which can get rid of unstable status.
		 */
		if (nau8825_is_jack_inserted(regmap)) {
			/* Turn off insertion interruption at manual mode */
			regmap_update_bits(regmap,
				NAU8825_REG_INTERRUPT_DIS_CTRL,
				NAU8825_IRQ_INSERT_DIS,
				NAU8825_IRQ_INSERT_DIS);
			regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
				NAU8825_IRQ_INSERT_EN, NAU8825_IRQ_INSERT_EN);
			/* Enable interruption for jack type detection at audo
			 * mode which can detect microphone and jack type.
			 */
			nau8825_setup_auto_irq(nau8825);
		}
	}

	if (!clear_irq)
		clear_irq = active_irq;
	/* clears the rightmost interruption */
	regmap_write(regmap, NAU8825_REG_INT_CLR_KEY_STATUS, clear_irq);

	/* Delay jack report until cross talk detection is done. It can avoid
	 * application to do playback preparation when cross talk detection
	 * process is still working. Otherwise, the resource like clock and
	 * power will be issued by them at the same time and conflict happens.
	 */
	if (event_mask && nau8825->xtalk_state == NAU8825_XTALK_DONE)
		snd_soc_jack_report(nau8825->jack, event, event_mask);

	return IRQ_HANDLED;
}

static void nau8825_setup_buttons(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;

	regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_TRACKING_GAIN_MASK,
		nau8825->sar_voltage << NAU8825_SAR_TRACKING_GAIN_SFT);
	regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_COMPARE_TIME_MASK,
		nau8825->sar_compare_time << NAU8825_SAR_COMPARE_TIME_SFT);
	regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_SAMPLING_TIME_MASK,
		nau8825->sar_sampling_time << NAU8825_SAR_SAMPLING_TIME_SFT);

	regmap_update_bits(regmap, NAU8825_REG_KEYDET_CTRL,
		NAU8825_KEYDET_LEVELS_NR_MASK,
		(nau8825->sar_threshold_num - 1) << NAU8825_KEYDET_LEVELS_NR_SFT);
	regmap_update_bits(regmap, NAU8825_REG_KEYDET_CTRL,
		NAU8825_KEYDET_HYSTERESIS_MASK,
		nau8825->sar_hysteresis << NAU8825_KEYDET_HYSTERESIS_SFT);
	regmap_update_bits(regmap, NAU8825_REG_KEYDET_CTRL,
		NAU8825_KEYDET_SHORTKEY_DEBOUNCE_MASK,
		nau8825->key_debounce << NAU8825_KEYDET_SHORTKEY_DEBOUNCE_SFT);

	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_1,
		(nau8825->sar_threshold[0] << 8) | nau8825->sar_threshold[1]);
	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_2,
		(nau8825->sar_threshold[2] << 8) | nau8825->sar_threshold[3]);
	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_3,
		(nau8825->sar_threshold[4] << 8) | nau8825->sar_threshold[5]);
	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_4,
		(nau8825->sar_threshold[6] << 8) | nau8825->sar_threshold[7]);

	/* Enable short press and release interruptions */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_KEY_SHORT_PRESS_EN | NAU8825_IRQ_KEY_RELEASE_EN,
		0);
}

static void nau8825_init_regs(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;

	/* Latch IIC LSB value */
	regmap_write(regmap, NAU8825_REG_IIC_ADDR_SET, 0x0001);
	/* Enable Bias/Vmid */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_VMID, NAU8825_BIAS_VMID);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BOOST,
		NAU8825_GLOBAL_BIAS_EN, NAU8825_GLOBAL_BIAS_EN);

	/* VMID Tieoff */
	regmap_update_bits(regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_VMID_SEL_MASK,
		nau8825->vref_impedance << NAU8825_BIAS_VMID_SEL_SFT);
	/* Disable Boost Driver, Automatic Short circuit protection enable */
	regmap_update_bits(regmap, NAU8825_REG_BOOST,
		NAU8825_PRECHARGE_DIS | NAU8825_HP_BOOST_DIS |
		NAU8825_HP_BOOST_G_DIS | NAU8825_SHORT_SHUTDOWN_EN,
		NAU8825_PRECHARGE_DIS | NAU8825_HP_BOOST_DIS |
		NAU8825_HP_BOOST_G_DIS | NAU8825_SHORT_SHUTDOWN_EN);

	regmap_update_bits(regmap, NAU8825_REG_GPIO12_CTRL,
		NAU8825_JKDET_OUTPUT_EN,
		nau8825->jkdet_enable ? 0 : NAU8825_JKDET_OUTPUT_EN);
	regmap_update_bits(regmap, NAU8825_REG_GPIO12_CTRL,
		NAU8825_JKDET_PULL_EN,
		nau8825->jkdet_pull_enable ? 0 : NAU8825_JKDET_PULL_EN);
	regmap_update_bits(regmap, NAU8825_REG_GPIO12_CTRL,
		NAU8825_JKDET_PULL_UP,
		nau8825->jkdet_pull_up ? NAU8825_JKDET_PULL_UP : 0);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_POLARITY,
		/* jkdet_polarity - 1  is for active-low */
		nau8825->jkdet_polarity ? 0 : NAU8825_JACK_POLARITY);

	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_INSERT_DEBOUNCE_MASK,
		nau8825->jack_insert_debounce << NAU8825_JACK_INSERT_DEBOUNCE_SFT);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_EJECT_DEBOUNCE_MASK,
		nau8825->jack_eject_debounce << NAU8825_JACK_EJECT_DEBOUNCE_SFT);

	/* Mask unneeded IRQs: 1 - disable, 0 - enable */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK, 0x7ff, 0x7ff);

	regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
		NAU8825_MICBIAS_VOLTAGE_MASK, nau8825->micbias_voltage);

	if (nau8825->sar_threshold_num)
		nau8825_setup_buttons(nau8825);

	/* Default oversampling/decimations settings are unusable
	 * (audible hiss). Set it to something better.
	 */
	regmap_update_bits(regmap, NAU8825_REG_ADC_RATE,
		NAU8825_ADC_SYNC_DOWN_MASK | NAU8825_ADC_SINC4_EN,
		NAU8825_ADC_SYNC_DOWN_64);
	regmap_update_bits(regmap, NAU8825_REG_DAC_CTRL1,
		NAU8825_DAC_OVERSAMPLE_MASK, NAU8825_DAC_OVERSAMPLE_64);
	/* Disable DACR/L power */
	regmap_update_bits(regmap, NAU8825_REG_CHARGE_PUMP,
		NAU8825_POWER_DOWN_DACR | NAU8825_POWER_DOWN_DACL,
		NAU8825_POWER_DOWN_DACR | NAU8825_POWER_DOWN_DACL);
	/* Enable TESTDAC. This sets the analog DAC inputs to a '0' input
	 * signal to avoid any glitches due to power up transients in both
	 * the analog and digital DAC circuit.
	 */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_TESTDAC_EN, NAU8825_BIAS_TESTDAC_EN);
	/* CICCLP off */
	regmap_update_bits(regmap, NAU8825_REG_DAC_CTRL1,
		NAU8825_DAC_CLIP_OFF, NAU8825_DAC_CLIP_OFF);

	/* Class AB bias current to 2x, DAC Capacitor enable MSB/LSB */
	regmap_update_bits(regmap, NAU8825_REG_ANALOG_CONTROL_2,
		NAU8825_HP_NON_CLASSG_CURRENT_2xADJ |
		NAU8825_DAC_CAPACITOR_MSB | NAU8825_DAC_CAPACITOR_LSB,
		NAU8825_HP_NON_CLASSG_CURRENT_2xADJ |
		NAU8825_DAC_CAPACITOR_MSB | NAU8825_DAC_CAPACITOR_LSB);
	/* Class G timer 64ms */
	regmap_update_bits(regmap, NAU8825_REG_CLASSG_CTRL,
		NAU8825_CLASSG_TIMER_MASK,
		0x20 << NAU8825_CLASSG_TIMER_SFT);
	/* DAC clock delay 2ns, VREF */
	regmap_update_bits(regmap, NAU8825_REG_RDAC,
		NAU8825_RDAC_CLK_DELAY_MASK | NAU8825_RDAC_VREF_MASK,
		(0x2 << NAU8825_RDAC_CLK_DELAY_SFT) |
		(0x3 << NAU8825_RDAC_VREF_SFT));
	/* Config L/R channel */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_DACL_CTRL,
		NAU8825_DACL_CH_SEL_MASK, NAU8825_DACL_CH_SEL_L);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_DACR_CTRL,
		NAU8825_DACL_CH_SEL_MASK, NAU8825_DACL_CH_SEL_R);
	/* Disable short Frame Sync detection logic */
	regmap_update_bits(regmap, NAU8825_REG_LEFT_TIME_SLOT,
		NAU8825_DIS_FS_SHORT_DET, NAU8825_DIS_FS_SHORT_DET);
}

static const struct regmap_config nau8825_regmap_config = {
	.val_bits = NAU8825_REG_DATA_LEN,
	.reg_bits = NAU8825_REG_ADDR_LEN,

	.max_register = NAU8825_REG_MAX,
	.readable_reg = nau8825_readable_reg,
	.writeable_reg = nau8825_writeable_reg,
	.volatile_reg = nau8825_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = nau8825_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(nau8825_reg_defaults),
};

static int nau8825_codec_probe(struct snd_soc_codec *codec)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

	nau8825->dapm = dapm;

	return 0;
}

static int nau8825_codec_remove(struct snd_soc_codec *codec)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	/* Cancel and reset cross tak suppresstion detection funciton */
	nau8825_xtalk_cancel(nau8825);

	return 0;
}

/**
 * nau8825_calc_fll_param - Calculate FLL parameters.
 * @fll_in: external clock provided to codec.
 * @fs: sampling rate.
 * @fll_param: Pointer to structure of FLL parameters.
 *
 * Calculate FLL parameters to configure codec.
 *
 * Returns 0 for success or negative error code.
 */
static int nau8825_calc_fll_param(unsigned int fll_in, unsigned int fs,
		struct nau8825_fll *fll_param)
{
	u64 fvco, fvco_max;
	unsigned int fref, i, fvco_sel;

	/* Ensure the reference clock frequency (FREF) is <= 13.5MHz by dividing
	 * freq_in by 1, 2, 4, or 8 using FLL pre-scalar.
	 * FREF = freq_in / NAU8825_FLL_REF_DIV_MASK
	 */
	for (i = 0; i < ARRAY_SIZE(fll_pre_scalar); i++) {
		fref = fll_in / fll_pre_scalar[i].param;
		if (fref <= NAU_FREF_MAX)
			break;
	}
	if (i == ARRAY_SIZE(fll_pre_scalar))
		return -EINVAL;
	fll_param->clk_ref_div = fll_pre_scalar[i].val;

	/* Choose the FLL ratio based on FREF */
	for (i = 0; i < ARRAY_SIZE(fll_ratio); i++) {
		if (fref >= fll_ratio[i].param)
			break;
	}
	if (i == ARRAY_SIZE(fll_ratio))
		return -EINVAL;
	fll_param->ratio = fll_ratio[i].val;

	/* Calculate the frequency of DCO (FDCO) given freq_out = 256 * Fs.
	 * FDCO must be within the 90MHz - 124MHz or the FFL cannot be
	 * guaranteed across the full range of operation.
	 * FDCO = freq_out * 2 * mclk_src_scaling
	 */
	fvco_max = 0;
	fvco_sel = ARRAY_SIZE(mclk_src_scaling);
	for (i = 0; i < ARRAY_SIZE(mclk_src_scaling); i++) {
		fvco = 256 * fs * 2 * mclk_src_scaling[i].param;
		if (fvco > NAU_FVCO_MIN && fvco < NAU_FVCO_MAX &&
			fvco_max < fvco) {
			fvco_max = fvco;
			fvco_sel = i;
		}
	}
	if (ARRAY_SIZE(mclk_src_scaling) == fvco_sel)
		return -EINVAL;
	fll_param->mclk_src = mclk_src_scaling[fvco_sel].val;

	/* Calculate the FLL 10-bit integer input and the FLL 16-bit fractional
	 * input based on FDCO, FREF and FLL ratio.
	 */
	fvco = div_u64(fvco_max << 16, fref * fll_param->ratio);
	fll_param->fll_int = (fvco >> 16) & 0x3FF;
	fll_param->fll_frac = fvco & 0xFFFF;
	return 0;
}

static void nau8825_fll_apply(struct nau8825 *nau8825,
		struct nau8825_fll *fll_param)
{
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
		NAU8825_CLK_SRC_MASK | NAU8825_CLK_MCLK_SRC_MASK,
		NAU8825_CLK_SRC_MCLK | fll_param->mclk_src);
	/* Make DSP operate at high speed for better performance. */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL1,
		NAU8825_FLL_RATIO_MASK | NAU8825_ICTRL_LATCH_MASK,
		fll_param->ratio | (0x6 << NAU8825_ICTRL_LATCH_SFT));
	/* FLL 16-bit fractional input */
	regmap_write(nau8825->regmap, NAU8825_REG_FLL2, fll_param->fll_frac);
	/* FLL 10-bit integer input */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_INTEGER_MASK, fll_param->fll_int);
	/* FLL pre-scaler */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL4,
			NAU8825_FLL_REF_DIV_MASK,
			fll_param->clk_ref_div << NAU8825_FLL_REF_DIV_SFT);
	/* select divided VCO input */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL5,
		NAU8825_FLL_CLK_SW_MASK, NAU8825_FLL_CLK_SW_REF);
	/* Disable free-running mode */
	regmap_update_bits(nau8825->regmap,
		NAU8825_REG_FLL6, NAU8825_DCO_EN, 0);
	if (fll_param->fll_frac) {
		/* set FLL loop filter enable and cutoff frequency at 500Khz */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL5,
			NAU8825_FLL_PDB_DAC_EN | NAU8825_FLL_LOOP_FTR_EN |
			NAU8825_FLL_FTR_SW_MASK,
			NAU8825_FLL_PDB_DAC_EN | NAU8825_FLL_LOOP_FTR_EN |
			NAU8825_FLL_FTR_SW_FILTER);
		regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL6,
			NAU8825_SDM_EN | NAU8825_CUTOFF500,
			NAU8825_SDM_EN | NAU8825_CUTOFF500);
	} else {
		/* disable FLL loop filter and cutoff frequency */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL5,
			NAU8825_FLL_PDB_DAC_EN | NAU8825_FLL_LOOP_FTR_EN |
			NAU8825_FLL_FTR_SW_MASK, NAU8825_FLL_FTR_SW_ACCU);
		regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL6,
			NAU8825_SDM_EN | NAU8825_CUTOFF500, 0);
	}
}

/* freq_out must be 256*Fs in order to achieve the best performance */
static int nau8825_set_pll(struct snd_soc_codec *codec, int pll_id, int source,
		unsigned int freq_in, unsigned int freq_out)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	struct nau8825_fll fll_param;
	int ret, fs;

	fs = freq_out / 256;
	ret = nau8825_calc_fll_param(freq_in, fs, &fll_param);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupported input clock %d\n", freq_in);
		return ret;
	}
	dev_dbg(codec->dev, "mclk_src=%x ratio=%x fll_frac=%x fll_int=%x clk_ref_div=%x\n",
		fll_param.mclk_src, fll_param.ratio, fll_param.fll_frac,
		fll_param.fll_int, fll_param.clk_ref_div);

	nau8825_fll_apply(nau8825, &fll_param);
	mdelay(2);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
			NAU8825_CLK_SRC_MASK, NAU8825_CLK_SRC_VCO);
	return 0;
}

static int nau8825_mclk_prepare(struct nau8825 *nau8825, unsigned int freq)
{
	int ret = 0;

	nau8825->mclk = devm_clk_get(nau8825->dev, "mclk");
	if (IS_ERR(nau8825->mclk)) {
		dev_info(nau8825->dev, "No 'mclk' clock found, assume MCLK is managed externally");
		return 0;
	}

	if (!nau8825->mclk_freq) {
		ret = clk_prepare_enable(nau8825->mclk);
		if (ret) {
			dev_err(nau8825->dev, "Unable to prepare codec mclk\n");
			return ret;
		}
	}

	if (nau8825->mclk_freq != freq) {
		freq = clk_round_rate(nau8825->mclk, freq);
		ret = clk_set_rate(nau8825->mclk, freq);
		if (ret) {
			dev_err(nau8825->dev, "Unable to set mclk rate\n");
			return ret;
		}
		nau8825->mclk_freq = freq;
	}

	return 0;
}

static void nau8825_configure_mclk_as_sysclk(struct regmap *regmap)
{
	regmap_update_bits(regmap, NAU8825_REG_CLK_DIVIDER,
		NAU8825_CLK_SRC_MASK, NAU8825_CLK_SRC_MCLK);
	regmap_update_bits(regmap, NAU8825_REG_FLL6,
		NAU8825_DCO_EN, 0);
	/* Make DSP operate as default setting for power saving. */
	regmap_update_bits(regmap, NAU8825_REG_FLL1,
		NAU8825_ICTRL_LATCH_MASK, 0);
}

static int nau8825_configure_sysclk(struct nau8825 *nau8825, int clk_id,
	unsigned int freq)
{
	struct regmap *regmap = nau8825->regmap;
	int ret;

	switch (clk_id) {
	case NAU8825_CLK_DIS:
		/* Clock provided externally and disable internal VCO clock */
		nau8825_configure_mclk_as_sysclk(regmap);
		if (nau8825->mclk_freq) {
			clk_disable_unprepare(nau8825->mclk);
			nau8825->mclk_freq = 0;
		}

		break;
	case NAU8825_CLK_MCLK:
		/* Acquire the semaphore to synchronize the playback and
		 * interrupt handler. In order to avoid the playback inter-
		 * fered by cross talk process, the driver make the playback
		 * preparation halted until cross talk process finish.
		 */
		nau8825_sema_acquire(nau8825, 3 * HZ);
		nau8825_configure_mclk_as_sysclk(regmap);
		/* MCLK not changed by clock tree */
		regmap_update_bits(regmap, NAU8825_REG_CLK_DIVIDER,
			NAU8825_CLK_MCLK_SRC_MASK, 0);
		/* Release the semaphore. */
		nau8825_sema_release(nau8825);

		ret = nau8825_mclk_prepare(nau8825, freq);
		if (ret)
			return ret;

		break;
	case NAU8825_CLK_INTERNAL:
		if (nau8825_is_jack_inserted(nau8825->regmap)) {
			regmap_update_bits(regmap, NAU8825_REG_FLL6,
				NAU8825_DCO_EN, NAU8825_DCO_EN);
			regmap_update_bits(regmap, NAU8825_REG_CLK_DIVIDER,
				NAU8825_CLK_SRC_MASK, NAU8825_CLK_SRC_VCO);
			/* Decrease the VCO frequency and make DSP operate
			 * as default setting for power saving.
			 */
			regmap_update_bits(regmap, NAU8825_REG_CLK_DIVIDER,
				NAU8825_CLK_MCLK_SRC_MASK, 0xf);
			regmap_update_bits(regmap, NAU8825_REG_FLL1,
				NAU8825_ICTRL_LATCH_MASK |
				NAU8825_FLL_RATIO_MASK, 0x10);
			regmap_update_bits(regmap, NAU8825_REG_FLL6,
				NAU8825_SDM_EN, NAU8825_SDM_EN);
		} else {
			/* The clock turns off intentionally for power saving
			 * when no headset connected.
			 */
			nau8825_configure_mclk_as_sysclk(regmap);
			dev_warn(nau8825->dev, "Disable clock for power saving when no headset connected\n");
		}
		if (nau8825->mclk_freq) {
			clk_disable_unprepare(nau8825->mclk);
			nau8825->mclk_freq = 0;
		}

		break;
	case NAU8825_CLK_FLL_MCLK:
		/* Acquire the semaphore to synchronize the playback and
		 * interrupt handler. In order to avoid the playback inter-
		 * fered by cross talk process, the driver make the playback
		 * preparation halted until cross talk process finish.
		 */
		nau8825_sema_acquire(nau8825, 3 * HZ);
		/* Higher FLL reference input frequency can only set lower
		 * gain error, such as 0000 for input reference from MCLK
		 * 12.288Mhz.
		 */
		regmap_update_bits(regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_CLK_SRC_MASK | NAU8825_GAIN_ERR_MASK,
			NAU8825_FLL_CLK_SRC_MCLK | 0);
		/* Release the semaphore. */
		nau8825_sema_release(nau8825);

		ret = nau8825_mclk_prepare(nau8825, freq);
		if (ret)
			return ret;

		break;
	case NAU8825_CLK_FLL_BLK:
		/* Acquire the semaphore to synchronize the playback and
		 * interrupt handler. In order to avoid the playback inter-
		 * fered by cross talk process, the driver make the playback
		 * preparation halted until cross talk process finish.
		 */
		nau8825_sema_acquire(nau8825, 3 * HZ);
		/* If FLL reference input is from low frequency source,
		 * higher error gain can apply such as 0xf which has
		 * the most sensitive gain error correction threshold,
		 * Therefore, FLL has the most accurate DCO to
		 * target frequency.
		 */
		regmap_update_bits(regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_CLK_SRC_MASK | NAU8825_GAIN_ERR_MASK,
			NAU8825_FLL_CLK_SRC_BLK |
			(0xf << NAU8825_GAIN_ERR_SFT));
		/* Release the semaphore. */
		nau8825_sema_release(nau8825);

		if (nau8825->mclk_freq) {
			clk_disable_unprepare(nau8825->mclk);
			nau8825->mclk_freq = 0;
		}

		break;
	case NAU8825_CLK_FLL_FS:
		/* Acquire the semaphore to synchronize the playback and
		 * interrupt handler. In order to avoid the playback inter-
		 * fered by cross talk process, the driver make the playback
		 * preparation halted until cross talk process finish.
		 */
		nau8825_sema_acquire(nau8825, 3 * HZ);
		/* If FLL reference input is from low frequency source,
		 * higher error gain can apply such as 0xf which has
		 * the most sensitive gain error correction threshold,
		 * Therefore, FLL has the most accurate DCO to
		 * target frequency.
		 */
		regmap_update_bits(regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_CLK_SRC_MASK | NAU8825_GAIN_ERR_MASK,
			NAU8825_FLL_CLK_SRC_FS |
			(0xf << NAU8825_GAIN_ERR_SFT));
		/* Release the semaphore. */
		nau8825_sema_release(nau8825);

		if (nau8825->mclk_freq) {
			clk_disable_unprepare(nau8825->mclk);
			nau8825->mclk_freq = 0;
		}

		break;
	default:
		dev_err(nau8825->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	dev_dbg(nau8825->dev, "Sysclk is %dHz and clock id is %d\n", freq,
		clk_id);
	return 0;
}

static int nau8825_set_sysclk(struct snd_soc_codec *codec, int clk_id,
	int source, unsigned int freq, int dir)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	return nau8825_configure_sysclk(nau8825, clk_id, freq);
}

static int nau8825_resume_setup(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;

	/* Close clock when jack type detection at manual mode */
	nau8825_configure_sysclk(nau8825, NAU8825_CLK_DIS, 0);

	/* Clear all interruption status */
	nau8825_int_status_clear_all(regmap);

	/* Enable both insertion and ejection interruptions, and then
	 * bypass de-bounce circuit.
	 */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_OUTPUT_EN | NAU8825_IRQ_HEADSET_COMPLETE_EN |
		NAU8825_IRQ_EJECT_EN | NAU8825_IRQ_INSERT_EN,
		NAU8825_IRQ_OUTPUT_EN | NAU8825_IRQ_HEADSET_COMPLETE_EN);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_DET_DB_BYPASS, NAU8825_JACK_DET_DB_BYPASS);
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_DIS_CTRL,
		NAU8825_IRQ_INSERT_DIS | NAU8825_IRQ_EJECT_DIS, 0);

	return 0;
}

static int nau8825_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			if (nau8825->mclk_freq) {
				ret = clk_prepare_enable(nau8825->mclk);
				if (ret) {
					dev_err(nau8825->dev, "Unable to prepare codec mclk\n");
					return ret;
				}
			}
			/* Setup codec configuration after resume */
			nau8825_resume_setup(nau8825);
		}
		break;

	case SND_SOC_BIAS_OFF:
		/* Reset the configuration of jack type for detection */
		/* Detach 2kOhm Resistors from MICBIAS to MICGND1/2 */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_MIC_BIAS,
			NAU8825_MICBIAS_JKSLV | NAU8825_MICBIAS_JKR2, 0);
		/* ground HPL/HPR, MICGRND1/2 */
		regmap_update_bits(nau8825->regmap,
			NAU8825_REG_HSD_CTRL, 0xf, 0xf);
		/* Cancel and reset cross talk detection funciton */
		nau8825_xtalk_cancel(nau8825);
		/* Turn off all interruptions before system shutdown. Keep the
		 * interruption quiet before resume setup completes.
		 */
		regmap_write(nau8825->regmap,
			NAU8825_REG_INTERRUPT_DIS_CTRL, 0xffff);
		/* Disable ADC needed for interruptions at audo mode */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_ENA_CTRL,
			NAU8825_ENABLE_ADC, 0);
		if (nau8825->mclk_freq)
			clk_disable_unprepare(nau8825->mclk);
		break;
	}
	return 0;
}

static int __maybe_unused nau8825_suspend(struct snd_soc_codec *codec)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	disable_irq(nau8825->irq);
	snd_soc_codec_force_bias_level(codec, SND_SOC_BIAS_OFF);
	/* Power down codec power; don't suppoet button wakeup */
	snd_soc_dapm_disable_pin(nau8825->dapm, "SAR");
	snd_soc_dapm_disable_pin(nau8825->dapm, "MICBIAS");
	snd_soc_dapm_sync(nau8825->dapm);
	regcache_cache_only(nau8825->regmap, true);
	regcache_mark_dirty(nau8825->regmap);

	return 0;
}

static int __maybe_unused nau8825_resume(struct snd_soc_codec *codec)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	int ret;

	regcache_cache_only(nau8825->regmap, false);
	regcache_sync(nau8825->regmap);
	nau8825->xtalk_protect = true;
	ret = nau8825_sema_acquire(nau8825, 0);
	if (ret < 0)
		nau8825->xtalk_protect = false;
	enable_irq(nau8825->irq);

	return 0;
}

static const struct snd_soc_codec_driver nau8825_codec_driver = {
	.probe = nau8825_codec_probe,
	.remove = nau8825_codec_remove,
	.set_sysclk = nau8825_set_sysclk,
	.set_pll = nau8825_set_pll,
	.set_bias_level = nau8825_set_bias_level,
	.suspend_bias_off = true,
	.suspend = nau8825_suspend,
	.resume = nau8825_resume,

	.component_driver = {
		.controls		= nau8825_controls,
		.num_controls		= ARRAY_SIZE(nau8825_controls),
		.dapm_widgets		= nau8825_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(nau8825_dapm_widgets),
		.dapm_routes		= nau8825_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(nau8825_dapm_routes),
	},
};

static void nau8825_reset_chip(struct regmap *regmap)
{
	regmap_write(regmap, NAU8825_REG_RESET, 0x00);
	regmap_write(regmap, NAU8825_REG_RESET, 0x00);
}

static void nau8825_print_device_properties(struct nau8825 *nau8825)
{
	int i;
	struct device *dev = nau8825->dev;

	dev_dbg(dev, "jkdet-enable:         %d\n", nau8825->jkdet_enable);
	dev_dbg(dev, "jkdet-pull-enable:    %d\n", nau8825->jkdet_pull_enable);
	dev_dbg(dev, "jkdet-pull-up:        %d\n", nau8825->jkdet_pull_up);
	dev_dbg(dev, "jkdet-polarity:       %d\n", nau8825->jkdet_polarity);
	dev_dbg(dev, "micbias-voltage:      %d\n", nau8825->micbias_voltage);
	dev_dbg(dev, "vref-impedance:       %d\n", nau8825->vref_impedance);

	dev_dbg(dev, "sar-threshold-num:    %d\n", nau8825->sar_threshold_num);
	for (i = 0; i < nau8825->sar_threshold_num; i++)
		dev_dbg(dev, "sar-threshold[%d]=%d\n", i,
				nau8825->sar_threshold[i]);

	dev_dbg(dev, "sar-hysteresis:       %d\n", nau8825->sar_hysteresis);
	dev_dbg(dev, "sar-voltage:          %d\n", nau8825->sar_voltage);
	dev_dbg(dev, "sar-compare-time:     %d\n", nau8825->sar_compare_time);
	dev_dbg(dev, "sar-sampling-time:    %d\n", nau8825->sar_sampling_time);
	dev_dbg(dev, "short-key-debounce:   %d\n", nau8825->key_debounce);
	dev_dbg(dev, "jack-insert-debounce: %d\n",
			nau8825->jack_insert_debounce);
	dev_dbg(dev, "jack-eject-debounce:  %d\n",
			nau8825->jack_eject_debounce);
	dev_dbg(dev, "crosstalk-enable:     %d\n",
			nau8825->xtalk_enable);
}

static int nau8825_read_device_properties(struct device *dev,
	struct nau8825 *nau8825) {
	int ret;

	nau8825->jkdet_enable = device_property_read_bool(dev,
		"nuvoton,jkdet-enable");
	nau8825->jkdet_pull_enable = device_property_read_bool(dev,
		"nuvoton,jkdet-pull-enable");
	nau8825->jkdet_pull_up = device_property_read_bool(dev,
		"nuvoton,jkdet-pull-up");
	ret = device_property_read_u32(dev, "nuvoton,jkdet-polarity",
		&nau8825->jkdet_polarity);
	if (ret)
		nau8825->jkdet_polarity = 1;
	ret = device_property_read_u32(dev, "nuvoton,micbias-voltage",
		&nau8825->micbias_voltage);
	if (ret)
		nau8825->micbias_voltage = 6;
	ret = device_property_read_u32(dev, "nuvoton,vref-impedance",
		&nau8825->vref_impedance);
	if (ret)
		nau8825->vref_impedance = 2;
	ret = device_property_read_u32(dev, "nuvoton,sar-threshold-num",
		&nau8825->sar_threshold_num);
	if (ret)
		nau8825->sar_threshold_num = 4;
	ret = device_property_read_u32_array(dev, "nuvoton,sar-threshold",
		nau8825->sar_threshold, nau8825->sar_threshold_num);
	if (ret) {
		nau8825->sar_threshold[0] = 0x08;
		nau8825->sar_threshold[1] = 0x12;
		nau8825->sar_threshold[2] = 0x26;
		nau8825->sar_threshold[3] = 0x73;
	}
	ret = device_property_read_u32(dev, "nuvoton,sar-hysteresis",
		&nau8825->sar_hysteresis);
	if (ret)
		nau8825->sar_hysteresis = 0;
	ret = device_property_read_u32(dev, "nuvoton,sar-voltage",
		&nau8825->sar_voltage);
	if (ret)
		nau8825->sar_voltage = 6;
	ret = device_property_read_u32(dev, "nuvoton,sar-compare-time",
		&nau8825->sar_compare_time);
	if (ret)
		nau8825->sar_compare_time = 1;
	ret = device_property_read_u32(dev, "nuvoton,sar-sampling-time",
		&nau8825->sar_sampling_time);
	if (ret)
		nau8825->sar_sampling_time = 1;
	ret = device_property_read_u32(dev, "nuvoton,short-key-debounce",
		&nau8825->key_debounce);
	if (ret)
		nau8825->key_debounce = 3;
	ret = device_property_read_u32(dev, "nuvoton,jack-insert-debounce",
		&nau8825->jack_insert_debounce);
	if (ret)
		nau8825->jack_insert_debounce = 7;
	ret = device_property_read_u32(dev, "nuvoton,jack-eject-debounce",
		&nau8825->jack_eject_debounce);
	if (ret)
		nau8825->jack_eject_debounce = 0;
	nau8825->xtalk_enable = device_property_read_bool(dev,
		"nuvoton,crosstalk-enable");

	nau8825->mclk = devm_clk_get(dev, "mclk");
	if (PTR_ERR(nau8825->mclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (PTR_ERR(nau8825->mclk) == -ENOENT) {
		/* The MCLK is managed externally or not used at all */
		nau8825->mclk = NULL;
		dev_info(dev, "No 'mclk' clock found, assume MCLK is managed externally");
	} else if (IS_ERR(nau8825->mclk)) {
		return -EINVAL;
	}

	return 0;
}

static int nau8825_setup_irq(struct nau8825 *nau8825)
{
	int ret;

	ret = devm_request_threaded_irq(nau8825->dev, nau8825->irq, NULL,
		nau8825_interrupt, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
		"nau8825", nau8825);

	if (ret) {
		dev_err(nau8825->dev, "Cannot request irq %d (%d)\n",
			nau8825->irq, ret);
		return ret;
	}

	return 0;
}

static int nau8825_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct nau8825 *nau8825 = dev_get_platdata(&i2c->dev);
	int ret, value;

	if (!nau8825) {
		nau8825 = devm_kzalloc(dev, sizeof(*nau8825), GFP_KERNEL);
		if (!nau8825)
			return -ENOMEM;
		ret = nau8825_read_device_properties(dev, nau8825);
		if (ret)
			return ret;
	}

	i2c_set_clientdata(i2c, nau8825);

	nau8825->regmap = devm_regmap_init_i2c(i2c, &nau8825_regmap_config);
	if (IS_ERR(nau8825->regmap))
		return PTR_ERR(nau8825->regmap);
	nau8825->dev = dev;
	nau8825->irq = i2c->irq;
	/* Initiate parameters, semaphore and work queue which are needed in
	 * cross talk suppression measurment function.
	 */
	nau8825->xtalk_state = NAU8825_XTALK_DONE;
	nau8825->xtalk_protect = false;
	sema_init(&nau8825->xtalk_sem, 1);
	INIT_WORK(&nau8825->xtalk_work, nau8825_xtalk_work);

	nau8825_print_device_properties(nau8825);

	nau8825_reset_chip(nau8825->regmap);
	ret = regmap_read(nau8825->regmap, NAU8825_REG_I2C_DEVICE_ID, &value);
	if (ret < 0) {
		dev_err(dev, "Failed to read device id from the NAU8825: %d\n",
			ret);
		return ret;
	}
	if ((value & NAU8825_SOFTWARE_ID_MASK) !=
			NAU8825_SOFTWARE_ID_NAU8825) {
		dev_err(dev, "Not a NAU8825 chip\n");
		return -ENODEV;
	}

	nau8825_init_regs(nau8825);

	if (i2c->irq)
		nau8825_setup_irq(nau8825);

	return snd_soc_register_codec(&i2c->dev, &nau8825_codec_driver,
		&nau8825_dai, 1);
}

static int nau8825_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id nau8825_i2c_ids[] = {
	{ "nau8825", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nau8825_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id nau8825_of_ids[] = {
	{ .compatible = "nuvoton,nau8825", },
	{}
};
MODULE_DEVICE_TABLE(of, nau8825_of_ids);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id nau8825_acpi_match[] = {
	{ "10508825", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, nau8825_acpi_match);
#endif

static struct i2c_driver nau8825_driver = {
	.driver = {
		.name = "nau8825",
		.of_match_table = of_match_ptr(nau8825_of_ids),
		.acpi_match_table = ACPI_PTR(nau8825_acpi_match),
	},
	.probe = nau8825_i2c_probe,
	.remove = nau8825_i2c_remove,
	.id_table = nau8825_i2c_ids,
};
module_i2c_driver(nau8825_driver);

MODULE_DESCRIPTION("ASoC nau8825 driver");
MODULE_AUTHOR("Anatol Pomozov <anatol@chromium.org>");
MODULE_LICENSE("GPL");
