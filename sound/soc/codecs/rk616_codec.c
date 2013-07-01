/*
 * rk616.c  --  RK616 CODEC ALSA SoC audio driver
 *
 * Copyright 2013 Rockship
 * Author: chenjq <chenjq@rock-chips.com>
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
#include <linux/gpio.h>
#include <linux/mfd/rk616.h>
#include "rk616_codec.h"
#include <mach/board.h>


#define RK616_PROC
#ifdef RK616_PROC
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#endif

#ifdef CONFIG_RK_HEADSET_DET
#include "../../../drivers/headset_observe/rk_headset.h"
#endif

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

/* volume setting
 *  0: -39dB
 *  26: 0dB
 *  31: 6dB
 *  Step: 1.5dB
*/
#define  SPKOUT_VOLUME    22 //0~31
#define  HPOUT_VOLUME     21 //0~31

struct rk616_codec_priv {
	struct snd_soc_codec *codec;

	unsigned int stereo_sysclk;
	unsigned int rate;

	int playback_active;
	int capture_active;

	int spk_ctl_gpio;
	int hp_ctl_gpio;

	long int playback_path;
	long int capture_path;
	long int voice_call_path;
};

static struct rk616_codec_priv *rk616_priv = NULL;
static struct mfd_rk616 *rk616_mfd = NULL;

#define RK616_CODEC_ALL	0
#define RK616_CODEC_PALYBACK	1
#define RK616_CODEC_CAPTURE	2

#define RK616_CODEC_WORK_NULL	0
#define RK616_CODEC_WORK_POWER_DOWN	1
#define RK616_CODEC_WORK_POWER_UP	2

static struct workqueue_struct *rk616_codec_workq;

static void rk616_codec_capture_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(capture_delayed_work, rk616_codec_capture_work);
static int rk616_codec_work_capture_type = RK616_CODEC_WORK_NULL;
static bool rk616_for_mid = 1, is_hdmi_in = false;

static int board_for_mid(char *str)
{
	int val;

	val = simple_strtol(str,NULL,10);
	if (val == 0 || val == 1) {
		rk616_for_mid = (val ? 0 : 1);
		printk("%s : THIS IS FOR %s\n", __func__, rk616_for_mid ? "mid":"phone");
	} else {
		printk("%s : get board_ap_has_alsa error, val = %d\n", __func__, val);
	}

	return 0;
}
__setup("board_ap_has_alsa=",board_for_mid);

static const unsigned int rk616_reg_defaults[RK616_PGAR_AGC_CTL5 + 1] = {
	[RK616_RESET] = 0x0003,
	[RK616_DAC_VOL] = 0x0046,
	[RK616_ADC_INT_CTL1] = 0x0050,
	[RK616_ADC_INT_CTL2] = 0x000e,
	[RK616_DAC_INT_CTL1] = 0x0050,
	[RK616_DAC_INT_CTL2] = 0x000e,
	[RK616_CLK_CHPUMP] = 0x0021,
	[RK616_PGA_AGC_CTL] = 0x000c,
	[RK616_PWR_ADD1] = 0x007c,
	[RK616_BST_CTL] = 0x0099,
	[RK616_DIFFIN_CTL] = 0x0024,
	[RK616_MIXINL_CTL] = 0x001f,
	[RK616_MIXINL_VOL1] = 0x0024,
	[RK616_MIXINL_VOL2] = 0x0004,
	[RK616_MIXINR_CTL] = 0x003f,
	[RK616_MIXINR_VOL1] = 0x0024,
	[RK616_MIXINR_VOL2] = 0x0024,
	[RK616_PGAL_CTL] = 0x00cc,
	[RK616_PGAR_CTL] = 0x00cc,
	[RK616_PWR_ADD2] = 0x00ff,
	[RK616_DAC_CTL] = 0x003f,
	[RK616_LINEMIX_CTL] = 0x001f,
	[RK616_MUXHP_HPMIX_CTL] = 0x003c,
	[RK616_HPMIX_CTL] = 0x00ff,
	[RK616_HPMIX_VOL1] = 0x0000,
	[RK616_HPMIX_VOL2] = 0x0000,
	[RK616_LINEOUT1_CTL] = 0x0060,
	[RK616_LINEOUT2_CTL] = 0x0060,
	[RK616_SPKL_CTL] = 0x00e0,
	[RK616_SPKR_CTL] = 0x00e0,
	[RK616_HPL_CTL] = 0x00e0,
	[RK616_HPR_CTL] = 0x00e0,
	[RK616_MICBIAS_CTL] = 0x00ff,
	[RK616_MICKEY_DET_CTL] = 0x0028,
	[RK616_PWR_ADD3] = 0x000f,
	[RK616_ADC_CTL] = 0x0036,
	[RK616_SINGNAL_ZC_CTL1] = 0x003f,
	[RK616_SINGNAL_ZC_CTL2] = 0x00ff,
	[RK616_PGAL_AGC_CTL1] = 0x0010,
	[RK616_PGAL_AGC_CTL2] = 0x0025,
	[RK616_PGAL_AGC_CTL3] = 0x0041,
	[RK616_PGAL_AGC_CTL4] = 0x002c,
	[RK616_PGAL_ASR_CTL] = 0x0000,
	[RK616_PGAL_AGC_MAX_H] = 0x0026,
	[RK616_PGAL_AGC_MAX_L] = 0x0040,
	[RK616_PGAL_AGC_MIN_H] = 0x0036,
	[RK616_PGAL_AGC_MIN_L] = 0x0020,
	[RK616_PGAL_AGC_CTL5] = 0x0038,
	[RK616_PGAR_AGC_CTL1] = 0x0010,
	[RK616_PGAR_AGC_CTL2] = 0x0025,
	[RK616_PGAR_AGC_CTL3] = 0x0041,
	[RK616_PGAR_AGC_CTL4] = 0x002c,
	[RK616_PGAR_ASR_CTL] = 0x0000,
	[RK616_PGAR_AGC_MAX_H] = 0x0026,
	[RK616_PGAR_AGC_MAX_L] = 0x0040,
	[RK616_PGAR_AGC_MIN_H] = 0x0036,
	[RK616_PGAR_AGC_MIN_L] = 0x0020,
	[RK616_PGAR_AGC_CTL5] = 0x0038,
};

/* mfd registers default list */
static struct rk616_reg_val_typ rk616_mfd_reg_defaults[] = {
	{CRU_CODEC_DIV, 0x00000000},
	{CRU_IO_CON0, (I2S1_OUT_DISABLE | I2S0_OUT_DISABLE | I2S1_PD_DISABLE | I2S0_PD_DISABLE) |
		((I2S1_OUT_DISABLE | I2S0_OUT_DISABLE | I2S1_PD_DISABLE | I2S0_PD_DISABLE) << 16)},
	{CRU_IO_CON1, (I2S1_SI_EN | I2S0_SI_EN) | ((I2S1_SI_EN | I2S0_SI_EN) << 16)},
	{CRU_PCM2IS2_CON2, (0) | ((PCM_TO_I2S_MUX | APS_SEL | APS_CLR | I2S_CHANNEL_SEL) << 16)},
	{CRU_CFGMISC_CON, 0x00000000},
};

/* mfd registers cache list */
static struct rk616_reg_val_typ rk616_mfd_reg_cache[] = {
	{CRU_CODEC_DIV, 0x00000000},
	{CRU_IO_CON0, (I2S1_OUT_DISABLE | I2S0_OUT_DISABLE | I2S1_PD_DISABLE | I2S0_PD_DISABLE) |
		((I2S1_OUT_DISABLE | I2S0_OUT_DISABLE | I2S1_PD_DISABLE | I2S0_PD_DISABLE) << 16)},
	{CRU_IO_CON1, (I2S1_SI_EN | I2S0_SI_EN) | ((I2S1_SI_EN | I2S0_SI_EN) << 16)},
	{CRU_PCM2IS2_CON2, (0) | ((PCM_TO_I2S_MUX | APS_SEL | APS_CLR | I2S_CHANNEL_SEL) << 16)},
	{CRU_CFGMISC_CON, 0x00000000},
};
#define RK616_MFD_REG_LEN ARRAY_SIZE(rk616_mfd_reg_cache)

static int rk616_mfd_register(unsigned int reg)
{
	int i;

	for (i = 0; i < RK616_MFD_REG_LEN; i++) {
		if (rk616_mfd_reg_cache[i].reg == reg)
			return 1;
	}

	return 0;
}

/* If register's bit16-31 is mask bit add to this fun */
static int rk616_mfd_mask_register(unsigned int reg)
{
	switch (reg) {
	case CRU_IO_CON0:
	case CRU_IO_CON1:
	case CRU_PCM2IS2_CON2:
		return 1;
	default:
		return 0;
	}
}

static struct rk616_reg_val_typ rk616_mfd_codec_bit_list[] = {
	{CRU_CFGMISC_CON, AD_DA_LOOP | MICDET2_PIN_F_CODEC | MICDET1_PIN_F_CODEC},
};
#define RK616_MFD_CODEC_BIT_LEN ARRAY_SIZE(rk616_mfd_codec_bit_list)

static int rk616_mfd_codec_bit(unsigned int reg)
{
	int i;

	for (i = 0; i < RK616_MFD_CODEC_BIT_LEN; i++) {
		if (rk616_mfd_codec_bit_list[i].reg == reg)
			return i;
	}

	return -1;
}

static struct rk616_init_bit_typ rk616_init_bit_list[] = {
	{RK616_SPKL_CTL, RK616_MUTE, RK616_INIT_MASK},
	{RK616_SPKR_CTL, RK616_MUTE, RK616_INIT_MASK},
	{RK616_HPL_CTL, RK616_MUTE, RK616_INIT_MASK},
	{RK616_HPR_CTL, RK616_MUTE, RK616_INIT_MASK},
	{RK616_MUXHP_HPMIX_CTL, RK616_HML_PWRD, RK616_HML_INIT_MASK},
	{RK616_MUXHP_HPMIX_CTL, RK616_HMR_PWRD, RK616_HMR_INIT_MASK},
};
#define RK616_INIT_BIT_LIST_LEN ARRAY_SIZE(rk616_init_bit_list)

static int rk616_init_bit_register(unsigned int reg, int i)
{
	for (; i < RK616_INIT_BIT_LIST_LEN; i++) {
		if (rk616_init_bit_list[i].reg == reg)
			return i;
	}

	return -1;
}

static unsigned int rk616_codec_read(struct snd_soc_codec *codec, unsigned int reg);

static unsigned int rk616_set_init_value(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	unsigned int read_value, power_bit, set_bit;
	int i;

	// read codec init register
	i = rk616_init_bit_register(reg, 0);

	// set codec init bit
	// widget init bit should be setted 0 after widget power up or unmute,
	// and should be setted 1 after widget power down or mute.
	if (i >= 0) {
		read_value = rk616_codec_read(codec, reg);
		while (i >= 0) {
			power_bit = rk616_init_bit_list[i].power_bit;
			set_bit = rk616_init_bit_list[i].init_bit;

			if ((read_value & power_bit) != (value & power_bit))
				value = (value & ~set_bit) | ((value & power_bit) ?  set_bit : 0);

			i = rk616_init_bit_register(reg, ++i);
		}
	}

	return value;
}

static int rk616_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RK616_RESET:
	case RK616_CLK_CHPUMP:
	case RK616_MICKEY_DET_CTL:
	case CRU_CFGMISC_CON:
		return 1;
	default:
		return 0;
	}
}

static int rk616_codec_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RK616_RESET:
	case RK616_DAC_VOL:
	case RK616_ADC_INT_CTL1:
	case RK616_ADC_INT_CTL2:
	case RK616_DAC_INT_CTL1:
	case RK616_DAC_INT_CTL2:
	case RK616_CLK_CHPUMP:
	case RK616_PGA_AGC_CTL:
	case RK616_PWR_ADD1:
	case RK616_BST_CTL:
	case RK616_DIFFIN_CTL:
	case RK616_MIXINL_CTL:
	case RK616_MIXINL_VOL1:
	case RK616_MIXINL_VOL2:
	case RK616_MIXINR_CTL:
	case RK616_MIXINR_VOL1:
	case RK616_MIXINR_VOL2:
	case RK616_PGAL_CTL:
	case RK616_PGAR_CTL:
	case RK616_PWR_ADD2:
	case RK616_DAC_CTL:
	case RK616_LINEMIX_CTL:
	case RK616_MUXHP_HPMIX_CTL:
	case RK616_HPMIX_CTL:
	case RK616_HPMIX_VOL1:
	case RK616_HPMIX_VOL2:
	case RK616_LINEOUT1_CTL:
	case RK616_LINEOUT2_CTL:
	case RK616_SPKL_CTL:
	case RK616_SPKR_CTL:
	case RK616_HPL_CTL:
	case RK616_HPR_CTL:
	case RK616_MICBIAS_CTL:
	case RK616_MICKEY_DET_CTL:
	case RK616_PWR_ADD3:
	case RK616_ADC_CTL:
	case RK616_SINGNAL_ZC_CTL1:
	case RK616_SINGNAL_ZC_CTL2:
	case RK616_PGAL_AGC_CTL1:
	case RK616_PGAL_AGC_CTL2:
	case RK616_PGAL_AGC_CTL3:
	case RK616_PGAL_AGC_CTL4:
	case RK616_PGAL_ASR_CTL:
	case RK616_PGAL_AGC_MAX_H:
	case RK616_PGAL_AGC_MAX_L:
	case RK616_PGAL_AGC_MIN_H:
	case RK616_PGAL_AGC_MIN_L:
	case RK616_PGAL_AGC_CTL5:
	case RK616_PGAR_AGC_CTL1:
	case RK616_PGAR_AGC_CTL2:
	case RK616_PGAR_AGC_CTL3:
	case RK616_PGAR_AGC_CTL4:
	case RK616_PGAR_ASR_CTL:
	case RK616_PGAR_AGC_MAX_H:
	case RK616_PGAR_AGC_MAX_L:
	case RK616_PGAR_AGC_MIN_H:
	case RK616_PGAR_AGC_MIN_L:
	case RK616_PGAR_AGC_CTL5:
		return 1;
	default:
		return 0;
	}
}

static inline unsigned int rk616_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	unsigned int *cache = codec->reg_cache;
	int i;

	if (rk616_codec_register(codec, reg))
		return  cache[reg];

	if (rk616_mfd_register(reg)) {
		for (i = 0; i < RK616_MFD_REG_LEN; i++) {
			if (rk616_mfd_reg_cache[i].reg == reg)
				return rk616_mfd_reg_cache[i].value;
		}
	}

	printk("%s : reg error!\n", __func__);

	return -EINVAL;
}

static inline void rk616_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	unsigned int *cache = codec->reg_cache;
	int i;

	if (rk616_codec_register(codec, reg)) {
		cache[reg] = value;
		return;
	}

	if (rk616_mfd_register(reg)) {
		for (i = 0; i < RK616_MFD_REG_LEN; i++) {
			if (rk616_mfd_reg_cache[i].reg == reg) {
				rk616_mfd_reg_cache[i].value = value;
				return;
			}
		}
	}

	printk("%s : reg error!\n", __func__);
}

static unsigned int rk616_codec_read(struct snd_soc_codec *codec, unsigned int reg)
{
	struct mfd_rk616 *rk616 = rk616_mfd;
	unsigned int value;

	if (!rk616) {
		printk("%s : rk616 is NULL\n", __func__);
		return -EINVAL;
	}

	if (!rk616_mfd_register(reg) && !rk616_codec_register(codec, reg)) {
		printk("%s : reg error!\n", __func__);
		return -EINVAL;
	}

	if (rk616_volatile_register(codec, reg) == 0) {
		value = rk616_read_reg_cache(codec, reg);
	} else {
		if (rk616->read_dev(rk616, reg, &value) < 0) {
			printk("%s : reg = 0x%x failed\n", __func__, reg);
			return -EIO;
		}
	}

	DBG("%s : reg = 0x%x, val= 0x%x\n", __func__, reg, value);

	return value;
}

static int rk616_codec_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	struct mfd_rk616 *rk616 = rk616_mfd;
	unsigned int set_bit, read_value, new_value;
	int i;

	if (!rk616) {
		printk("%s : rk616 is NULL\n", __func__);
		return -EINVAL;
	} else if (!rk616_mfd_register(reg) && !rk616_codec_register(codec, reg)) {
		printk("%s : reg error!\n", __func__);
		return -EINVAL;
	}

	// set codec mask bit
	i = rk616_mfd_codec_bit(reg);
	if (i >= 0) {
		set_bit = rk616_mfd_codec_bit_list[i].value;
		read_value = rk616_codec_read(codec, reg);
		value = (read_value & ~set_bit) | (value & set_bit);
	} else if (rk616_mfd_mask_register(reg)) {
		value = ((0xffff0000 & rk616_read_reg_cache(codec, reg)) | (value & 0x0000ffff));
	}

	new_value = rk616_set_init_value(codec, reg, value);

	// write i2c
	if (rk616->write_dev(rk616, reg, &value) < 0) {
		printk("%s : reg = 0x%x failed\n", __func__, reg);
		return -EIO;
	}

	if (new_value != value) {
		if (rk616->write_dev(rk616, reg, &new_value) < 0) {
			printk("%s : reg = 0x%x failed\n", __func__, reg);
			return -EIO;
		}
		value = new_value;
	}

	rk616_write_reg_cache(codec, reg, value);

	DBG("%s : reg = 0x%x, val = 0x%x\n", __func__, reg, value);
	return 0;
}

static int rk616_hw_write(const struct i2c_client *client, const char *buf, int count)
{
	struct snd_soc_codec *codec = rk616_priv->codec;
	unsigned int reg, value;
	int ret = -1;

	if (!rk616_priv || !rk616_priv->codec) {
		printk("%s : rk616_priv or rk616_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	if (count == 3) {
		reg = (unsigned int)buf[0];
		value = (buf[1] & 0xff00) | (0x00ff & buf[2]);
		ret = rk616_codec_write(codec, reg, value);
	} else {
		printk("%s : i2c len error\n", __func__);
	}

	return (ret == 0) ? count : ret;
}

static int rk616_reset(struct snd_soc_codec *codec)
{
	int i;

	snd_soc_write(codec, RK616_RESET, 0xfc);
	mdelay(10);
	snd_soc_write(codec, RK616_RESET, 0x43);
	mdelay(10);

	for (i = 0; i < RK616_MFD_REG_LEN; i++)
		snd_soc_write(codec, rk616_mfd_reg_defaults[i].reg,
			rk616_mfd_reg_defaults[i].value);

	memcpy(codec->reg_cache, rk616_reg_defaults,
	       sizeof(rk616_reg_defaults));

	//close charge pump
	snd_soc_write(codec, RK616_CLK_CHPUMP, 0x41);

	//bypass zero-crossing detection
	snd_soc_write(codec, RK616_SINGNAL_ZC_CTL1, 0x3f);
	snd_soc_write(codec, RK616_SINGNAL_ZC_CTL2, 0xff);

	return 0;
}

int rk616_headset_mic_detect(bool headset_status)
{
	struct snd_soc_codec *codec = rk616_priv->codec;

	DBG("%s\n", __func__);

	if (!rk616_priv || !rk616_priv->codec) {
		printk("%s : rk616_priv or rk616_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	if (headset_status) {
		snd_soc_update_bits(codec, RK616_MICBIAS_CTL,
				RK616_MICBIAS2_PWRD | RK616_MICBIAS2_V_MASK,
				RK616_MICBIAS2_V_1_7);
	} else {// headset is out, disable MIC2 && MIC1 Bias
		DBG("%s : headset is out,disable Mic2 Bias\n", __func__);
		snd_soc_update_bits(codec, RK616_MICBIAS_CTL,
				RK616_MICBIAS2_PWRD | RK616_MICBIAS2_V_MASK,
				RK616_MICBIAS2_V_1_0|RK616_MICBIAS2_PWRD);
	}
	return 0;
}
EXPORT_SYMBOL(rk616_headset_mic_detect);

bool get_hdmi_state(void)
{
	return is_hdmi_in;
}

void codec_set_spk(bool on)
{
	struct snd_soc_codec *codec = rk616_priv->codec;

	DBG("%s : %s\n", __func__, on ? "enable spk" : "disable spk");

	if (!rk616_priv || !rk616_priv->codec) {
		printk("%s : rk616_priv or rk616_priv->codec is NULL\n", __func__);
		return;
	}

	if (on) {
		if (rk616_for_mid)
		{
			snd_soc_update_bits(codec, RK616_SPKL_CTL,
				RK616_MUTE, 0);
			snd_soc_update_bits(codec, RK616_SPKR_CTL,
				RK616_MUTE, 0);
			snd_soc_update_bits(codec, RK616_HPL_CTL,
				RK616_MUTE, 0);
			snd_soc_update_bits(codec, RK616_HPR_CTL,
				RK616_MUTE, 0);
		}
		else
		{
			snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack");
			snd_soc_dapm_enable_pin(&codec->dapm, "Ext Spk");
		}
	} else {
		if (rk616_priv->spk_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio LOW\n", __func__);
			gpio_set_value(rk616_priv->spk_ctl_gpio, GPIO_LOW);
		}

		if (rk616_priv->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set hp ctl gpio LOW\n", __func__);
			gpio_set_value(rk616_priv->hp_ctl_gpio, GPIO_LOW);
			snd_soc_write(codec, RK616_CLK_CHPUMP, 0x41);
		}

		if (rk616_for_mid)
		{
			snd_soc_update_bits(codec, RK616_SPKL_CTL,
				RK616_MUTE, RK616_MUTE);
			snd_soc_update_bits(codec, RK616_SPKR_CTL,
				RK616_MUTE, RK616_MUTE);
			snd_soc_update_bits(codec, RK616_HPL_CTL,
				RK616_MUTE, RK616_MUTE);
			snd_soc_update_bits(codec, RK616_HPR_CTL,
				RK616_MUTE, RK616_MUTE);
		}
		else
		{
			snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack");
			snd_soc_dapm_disable_pin(&codec->dapm, "Ext Spk");
		}
	}
	snd_soc_dapm_sync(&codec->dapm);

	is_hdmi_in = on ? 0 : 1;
}
EXPORT_SYMBOL_GPL(codec_set_spk);

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -3900, 150, 0);
static const DECLARE_TLV_DB_SCALE(pga_vol_tlv, -1800, 150, 0);
static const DECLARE_TLV_DB_SCALE(bst_vol_tlv, 0, 2000, 0);
static const DECLARE_TLV_DB_SCALE(mix_vol_tlv, -1200, 300, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_max_vol_tlv, -1350, 600, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_min_vol_tlv, -1800, 600, 0);

static const char *rk616_input_mode[] = {"Differential", "Single-Ended"}; 

static const char *rk616_micbias_ratio[] = {"1.0 Vref", "1.1 Vref",
		"1.2 Vref", "1.3 Vref", "1.4 Vref", "1.5 Vref", "1.6 Vref", "1.7 Vref",};

static const char *rk616_dis_en_sel[] = {"Disable", "Enable"};

static const char *rk616_mickey_range[] = {"100uA", "300uA",
		"500uA", "700uA", "900uA", "1100uA", "1300uA", "1500uA"};

static const char *rk616_pga_gain_control[] = {"Normal", "AGC"};

static const char *rk616_pga_agc_way[] = {"Normal", "Jack"};

static const char *rk616_pga_agc_hold_time[] = {"0ms", "2ms",
		"4ms", "8ms", "16ms", "32ms", "64ms", "128ms", "256ms", "512ms", "1s"};

static const char *rk616_pga_agc_ramp_up_time[] = {"500us", "1ms", "2ms",
		"4ms", "8ms", "16ms", "32ms", "64ms", "128ms", "256ms", "512ms"};

static const char *rk616_pga_agc_ramp_down_time[] = {"Normal:125us Jack:32us",
		"Normal:250us Jack:64us", "Normal:500us Jack:125us", "Normal:1ms Jack:250us",
		"Normal:2ms Jack:500us", "Normal:4ms Jack:1ms", "Normal:8ms Jack:2ms",
		"Normal:16ms Jack:4ms", "Normal:32ms Jack:8ms", "Normal:64ms Jack:16ms",
		"Normal:128ms Jack:32ms"};

static const char *rk616_pga_agc_mode[] = {"Normal", "Limiter"};

static const char *rk616_pga_agc_recovery_mode[] = {"Right Now", "After AGC to Limiter"};

static const char *rk616_pga_agc_noise_gate_threhold[] = {"-39dB", "-45dB", "-51dB",
		"-57dB", "-63dB", "-69dB", "-75dB", "-81dB"};

static const char *rk616_pga_agc_update_gain[] = {"Right Now", "After 1st Zero Cross"};

static const char *rk616_pga_agc_approximate_sample_rate[] = {"48KHz", "32KHz",
		"24KHz", "16KHz", "12KHz", "8KHz"};

static const struct soc_enum rk616_bst_enum[] = {
SOC_ENUM_SINGLE(RK616_BST_CTL, RK616_BSTL_MODE_SFT, 2, rk616_input_mode),
SOC_ENUM_SINGLE(RK616_BST_CTL, RK616_BSTR_MODE_SFT, 2, rk616_input_mode),
};

static const struct soc_enum rk616_diffin_enum =
	SOC_ENUM_SINGLE(RK616_DIFFIN_CTL, RK616_DIFFIN_MODE_SFT, 2, rk616_input_mode);

static const struct soc_enum rk616_micbias_enum[] = {
SOC_ENUM_SINGLE(RK616_MICBIAS_CTL, RK616_MICBIAS1_V_SFT, 8, rk616_micbias_ratio),
SOC_ENUM_SINGLE(RK616_MICBIAS_CTL, RK616_MICBIAS2_V_SFT, 8, rk616_micbias_ratio),
};

static const struct soc_enum rk616_mickey_enum[] = {
SOC_ENUM_SINGLE(RK616_MICKEY_DET_CTL, RK616_MK1_DET_SFT, 2, rk616_dis_en_sel),
SOC_ENUM_SINGLE(RK616_MICKEY_DET_CTL, RK616_MK2_DET_SFT, 2, rk616_dis_en_sel),
SOC_ENUM_SINGLE(RK616_MICKEY_DET_CTL, RK616_MK1_DET_I_SFT, 8, rk616_mickey_range),
SOC_ENUM_SINGLE(RK616_MICKEY_DET_CTL, RK616_MK2_DET_I_SFT, 8, rk616_mickey_range),
};

static const struct soc_enum rk616_agcl_enum[] = {
SOC_ENUM_SINGLE(RK616_PGA_AGC_CTL, RK616_PGAL_AGC_EN_SFT, 2, rk616_pga_gain_control),/*0*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL1, RK616_PGA_AGC_WAY_SFT, 2, rk616_pga_agc_way),/*1*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL1, RK616_PGA_AGC_HOLD_T_SFT, 11, rk616_pga_agc_hold_time),/*2*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL2, RK616_PGA_AGC_GRU_T_SFT, 11, rk616_pga_agc_ramp_up_time),/*3*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL2, RK616_PGA_AGC_GRD_T_SFT, 11, rk616_pga_agc_ramp_down_time),/*4*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL3, RK616_PGA_AGC_MODE_SFT, 2, rk616_pga_agc_mode),/*5*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL3, RK616_PGA_AGC_ZO_SFT, 2, rk616_dis_en_sel),/*6*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL3, RK616_PGA_AGC_REC_MODE_SFT, 2, rk616_pga_agc_recovery_mode),/*7*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL3, RK616_PGA_AGC_FAST_D_SFT, 2, rk616_dis_en_sel),/*8*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL3, RK616_PGA_AGC_NG_SFT, 2, rk616_dis_en_sel),/*9*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL3, RK616_PGA_AGC_NG_THR_SFT, 8, rk616_pga_agc_noise_gate_threhold),/*10*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL4, RK616_PGA_AGC_ZO_MODE_SFT, 2, rk616_pga_agc_update_gain),/*11*/
SOC_ENUM_SINGLE(RK616_PGAL_ASR_CTL, RK616_PGA_SLOW_CLK_SFT, 2, rk616_dis_en_sel),/*12*/
SOC_ENUM_SINGLE(RK616_PGAL_ASR_CTL, RK616_PGA_ASR_SFT, 6, rk616_pga_agc_approximate_sample_rate),/*13*/
SOC_ENUM_SINGLE(RK616_PGAL_AGC_CTL5, RK616_PGA_AGC_SFT, 2, rk616_dis_en_sel),/*14*/
};

static const struct soc_enum rk616_agcr_enum[] = {
SOC_ENUM_SINGLE(RK616_PGA_AGC_CTL, RK616_PGAR_AGC_EN_SFT, 2, rk616_pga_gain_control),/*0*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL1, RK616_PGA_AGC_WAY_SFT, 2, rk616_pga_agc_way),/*1*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL1, RK616_PGA_AGC_HOLD_T_SFT, 11, rk616_pga_agc_hold_time),/*2*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL2, RK616_PGA_AGC_GRU_T_SFT, 11, rk616_pga_agc_ramp_up_time),/*3*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL2, RK616_PGA_AGC_GRD_T_SFT, 11, rk616_pga_agc_ramp_down_time),/*4*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL3, RK616_PGA_AGC_MODE_SFT, 2, rk616_pga_agc_mode),/*5*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL3, RK616_PGA_AGC_ZO_SFT, 2, rk616_dis_en_sel),/*6*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL3, RK616_PGA_AGC_REC_MODE_SFT, 2, rk616_pga_agc_recovery_mode),/*7*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL3, RK616_PGA_AGC_FAST_D_SFT, 2, rk616_dis_en_sel),/*8*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL3, RK616_PGA_AGC_NG_SFT, 2, rk616_dis_en_sel),/*9*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL3, RK616_PGA_AGC_NG_THR_SFT, 8, rk616_pga_agc_noise_gate_threhold),/*10*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL4, RK616_PGA_AGC_ZO_MODE_SFT, 2, rk616_pga_agc_update_gain),/*11*/
SOC_ENUM_SINGLE(RK616_PGAR_ASR_CTL, RK616_PGA_SLOW_CLK_SFT, 2, rk616_dis_en_sel),/*12*/
SOC_ENUM_SINGLE(RK616_PGAR_ASR_CTL, RK616_PGA_ASR_SFT, 6, rk616_pga_agc_approximate_sample_rate),/*13*/
SOC_ENUM_SINGLE(RK616_PGAR_AGC_CTL5, RK616_PGA_AGC_SFT, 2, rk616_dis_en_sel),/*14*/
};

static const struct soc_enum rk616_loop_enum =
	SOC_ENUM_SINGLE(CRU_CFGMISC_CON, AD_DA_LOOP_SFT, 2, rk616_dis_en_sel);

int snd_soc_put_pgal_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;

	val = (ucontrol->value.integer.value[0] & mask);

	//set for capture pop noise
	if (val) {
		snd_soc_update_bits(codec, RK616_PGA_AGC_CTL, 0x0f, 0x09);
	}

	return snd_soc_put_volsw(kcontrol, ucontrol);
}

static const struct snd_kcontrol_new rk616_snd_controls[] = {
	//Add for set voice volume
	SOC_DOUBLE_R_TLV("Speaker Playback Volume", RK616_SPKL_CTL,
		RK616_SPKR_CTL, RK616_VOL_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE_R("Speaker Playback Switch", RK616_SPKL_CTL,
		RK616_SPKR_CTL, RK616_MUTE_SFT, 1, 1),

	SOC_DOUBLE_R_TLV("Headphone Playback Volume", RK616_HPL_CTL,
		RK616_HPR_CTL, RK616_VOL_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE_R("Headphone Playback Switch", RK616_HPL_CTL,
		RK616_HPR_CTL, RK616_MUTE_SFT, 1, 1),

	SOC_DOUBLE_R_TLV("Earpiece Playback Volume", RK616_HPL_CTL,
		RK616_HPR_CTL, RK616_VOL_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE_R("Earpiece Playback Switch", RK616_HPL_CTL,
		RK616_HPR_CTL, RK616_MUTE_SFT, 1, 1),

	SOC_SINGLE_TLV("LINEOUT1 Playback Volume", RK616_LINEOUT1_CTL,
		RK616_LINEOUT_VOL_SFT, 31, 0, out_vol_tlv),
	SOC_SINGLE("LINEOUT1 Playback Switch", RK616_LINEOUT1_CTL,
		RK616_LINEOUT_MUTE_SFT, 1, 1),
	SOC_SINGLE_TLV("LINEOUT2 Playback Volume", RK616_LINEOUT2_CTL,
		RK616_LINEOUT_VOL_SFT, 31, 0, out_vol_tlv),
	SOC_SINGLE("LINEOUT2 Playback Switch", RK616_LINEOUT2_CTL,
		RK616_LINEOUT_MUTE_SFT, 1, 1),

	SOC_SINGLE_TLV("PGAL Capture Volume", RK616_PGAL_CTL,
		RK616_PGA_VOL_SFT, 31, 0, pga_vol_tlv),//0x0a bit 5 is 0
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = "PGAL Capture Switch", \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_pgal_volsw, \
	.private_value =  SOC_SINGLE_VALUE(RK616_PGAL_CTL, RK616_PGA_MUTE_SFT, 1, 1)
	},
	SOC_SINGLE_TLV("PGAR Capture Volume", RK616_PGAR_CTL,
		RK616_PGA_VOL_SFT, 31, 0, pga_vol_tlv),//0x0a bit 4 is 0
	SOC_SINGLE("PGAR Capture Switch", RK616_PGAR_CTL,
		RK616_PGA_MUTE_SFT, 1, 1),

	SOC_SINGLE_TLV("DIFFIN Capture Volume", RK616_DIFFIN_CTL,
		RK616_DIFFIN_GAIN_SFT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("DIFFIN Capture Switch", RK616_DIFFIN_CTL,
		RK616_DIFFIN_MUTE_SFT, 1, 1),

	//Add for set capture mute
	SOC_SINGLE_TLV("Main Mic Capture Volume", RK616_BST_CTL,
		RK616_BSTL_GAIN_SFT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Main Mic Capture Switch", RK616_BST_CTL,
		RK616_BSTL_MUTE_SFT, 1, 1),
	SOC_SINGLE_TLV("Headset Mic Capture Volume", RK616_BST_CTL,
		RK616_BSTR_GAIN_SFT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Headset Mic Capture Switch", RK616_BST_CTL,
		RK616_BSTR_MUTE_SFT, 1, 1),

	SOC_ENUM("BST_L Mode",  rk616_bst_enum[0]),
	SOC_ENUM("BST_R Mode",  rk616_bst_enum[1]),
	SOC_ENUM("DIFFIN Mode",  rk616_diffin_enum),

	SOC_SINGLE_TLV("MUXMIC to MIXINL Volume", RK616_MIXINL_VOL1,
		RK616_MIL_F_MUX_VOL_SFT, 7, 0, mix_vol_tlv),
	SOC_SINGLE_TLV("IN1P to MIXINL Volume", RK616_MIXINL_VOL1,
		RK616_MIL_F_IN1P_VOL_SFT, 7, 0, mix_vol_tlv),
	SOC_SINGLE_TLV("IN3L to MIXINL Volume", RK616_MIXINL_VOL2,
		RK616_MIL_F_IN3L_VOL_SFT, 7, 0, mix_vol_tlv),

	SOC_SINGLE_TLV("MIXINR MUX to MIXINR Volume", RK616_MIXINR_VOL1,
		RK616_MIR_F_MIRM_VOL_SFT, 7, 0, mix_vol_tlv),
	SOC_SINGLE_TLV("IN3R to MIXINR Volume", RK616_MIXINR_VOL1,
		RK616_MIR_F_IN3R_VOL_SFT, 7, 0, mix_vol_tlv),
	SOC_SINGLE_TLV("MIC2N to MIXINR Volume", RK616_MIXINR_VOL2,
		RK616_MIR_F_MIC2N_VOL_SFT, 7, 0, mix_vol_tlv),
	SOC_SINGLE_TLV("IN1P to MIXINR Volume", RK616_MIXINR_VOL2,
		RK616_MIR_F_IN1P_VOL_SFT, 7, 0, mix_vol_tlv),

	SOC_SINGLE("MIXINL Switch", RK616_MIXINL_CTL,
		RK616_MIL_MUTE_SFT, 1, 1),
	SOC_SINGLE("MIXINR Switch", RK616_MIXINR_CTL,
		RK616_MIR_MUTE_SFT, 1, 1),

	SOC_SINGLE_TLV("IN1P to HPMIXL Volume", RK616_HPMIX_VOL1,
		RK616_HML_F_IN1P_VOL_SFT, 7, 0, mix_vol_tlv),
	SOC_SINGLE_TLV("HPMIX MUX to HPMIXL Volume", RK616_HPMIX_VOL2,
		RK616_HML_F_HMM_VOL_SFT, 7, 0, mix_vol_tlv),
	SOC_SINGLE_TLV("HPMIX MUX to MIXINR Volume", RK616_HPMIX_VOL2,
		RK616_HMR_F_HMM_VOL_SFT, 7, 0, mix_vol_tlv),

	SOC_ENUM("Micbias1 Voltage",  rk616_micbias_enum[0]),
	SOC_ENUM("Micbias2 Voltage",  rk616_micbias_enum[1]),

	SOC_ENUM("MIC1 Key Detection Enable",  rk616_mickey_enum[0]),
	SOC_ENUM("MIC2 Key Detection Enable",  rk616_mickey_enum[1]),
	SOC_ENUM("MIC1 Key Range",  rk616_mickey_enum[2]),
	SOC_ENUM("MIC2 Key Range",  rk616_mickey_enum[3]),

	SOC_ENUM("PGAL Gain Control",  rk616_agcl_enum[0]),
	SOC_ENUM("PGAL AGC Way",  rk616_agcl_enum[1]),
	SOC_ENUM("PGAL AGC Hold Time",  rk616_agcl_enum[2]),
	SOC_ENUM("PGAL AGC Ramp Up Time",  rk616_agcl_enum[3]),
	SOC_ENUM("PGAL AGC Ramp Down Time",  rk616_agcl_enum[4]),
	SOC_ENUM("PGAL AGC Mode",  rk616_agcl_enum[5]),
	SOC_ENUM("PGAL AGC Gain Update Zero Enable",  rk616_agcl_enum[6]),
	SOC_ENUM("PGAL AGC Gain Recovery LPGA VOL",  rk616_agcl_enum[7]),
	SOC_ENUM("PGAL AGC Fast Decrement Enable",  rk616_agcl_enum[8]),
	SOC_ENUM("PGAL AGC Noise Gate Enable",  rk616_agcl_enum[9]),
	SOC_ENUM("PGAL AGC Noise Gate Threhold",  rk616_agcl_enum[10]),
	SOC_ENUM("PGAL AGC Upate Gain",  rk616_agcl_enum[11]),
	SOC_ENUM("PGAL AGC Slow Clock Enable",  rk616_agcl_enum[12]),
	SOC_ENUM("PGAL AGC Approximate Sample Rate",  rk616_agcl_enum[13]),
	SOC_ENUM("PGAL AGC Enable",  rk616_agcl_enum[14]),

	SOC_SINGLE_TLV("PGAL AGC Volume", RK616_PGAL_AGC_CTL4,
		RK616_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),//AGC disable and 0x0a bit 5 is 1

	SOC_SINGLE("PGAL AGC Max Level High 8 Bits", RK616_PGAL_AGC_MAX_H,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Max Level Low 8 Bits", RK616_PGAL_AGC_MAX_L,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level High 8 Bits", RK616_PGAL_AGC_MIN_H,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level Low 8 Bits", RK616_PGAL_AGC_MIN_L,
		0, 255, 0),

	SOC_SINGLE_TLV("PGAL AGC Max Gain", RK616_PGAL_AGC_CTL5,
		RK616_PGA_AGC_MAX_G_SFT, 7, 0, pga_agc_max_vol_tlv),//AGC enable and 0x0a bit 5 is 1
	SOC_SINGLE_TLV("PGAL AGC Min Gain", RK616_PGAL_AGC_CTL5,
		RK616_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),//AGC enable and 0x0a bit 5 is 1

	SOC_ENUM("PGAR Gain Control",  rk616_agcr_enum[0]),
	SOC_ENUM("PGAR AGC Way",  rk616_agcr_enum[1]),
	SOC_ENUM("PGAR AGC Hold Time",  rk616_agcr_enum[2]),
	SOC_ENUM("PGAR AGC Ramp Up Time",  rk616_agcr_enum[3]),
	SOC_ENUM("PGAR AGC Ramp Down Time",  rk616_agcr_enum[4]),
	SOC_ENUM("PGAR AGC Mode",  rk616_agcr_enum[5]),
	SOC_ENUM("PGAR AGC Gain Update Zero Enable",  rk616_agcr_enum[6]),
	SOC_ENUM("PGAR AGC Gain Recovery LPGA VOL",  rk616_agcr_enum[7]),
	SOC_ENUM("PGAR AGC Fast Decrement Enable",  rk616_agcr_enum[8]),
	SOC_ENUM("PGAR AGC Noise Gate Enable",  rk616_agcr_enum[9]),
	SOC_ENUM("PGAR AGC Noise Gate Threhold",  rk616_agcr_enum[10]),
	SOC_ENUM("PGAR AGC Upate Gain",  rk616_agcr_enum[11]),
	SOC_ENUM("PGAR AGC Slow Clock Enable",  rk616_agcr_enum[12]),
	SOC_ENUM("PGAR AGC Approximate Sample Rate",  rk616_agcr_enum[13]),
	SOC_ENUM("PGAR AGC Enable",  rk616_agcr_enum[14]),

	SOC_SINGLE_TLV("PGAR AGC Volume", RK616_PGAR_AGC_CTL4,
		RK616_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),//AGC disable and 0x0a bit 4 is 1

	SOC_SINGLE("PGAR AGC Max Level High 8 Bits", RK616_PGAR_AGC_MAX_H,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Max Level Low 8 Bits", RK616_PGAR_AGC_MAX_L,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level High 8 Bits", RK616_PGAR_AGC_MIN_H,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level Low 8 Bits", RK616_PGAR_AGC_MIN_L,
		0, 255, 0),

	SOC_SINGLE_TLV("PGAR AGC Max Gain", RK616_PGAR_AGC_CTL5,
		RK616_PGA_AGC_MAX_G_SFT, 7, 0, pga_agc_max_vol_tlv),//AGC enable and 0x06 bit 4 is 1
	SOC_SINGLE_TLV("PGAR AGC Min Gain", RK616_PGAR_AGC_CTL5,
		RK616_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),//AGC enable and 0x06 bit 4 is 1

	SOC_ENUM("I2S Loop Enable",  rk616_loop_enum),
};

//For tiny alsa playback/capture/voice call path
static const char *rk616_playback_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP", //0-6
		"RING_SPK", "RING_HP", "RING_HP_NO_MIC", "RING_SPK_HP"};//7-10

static const char *rk616_capture_path_mode[] = {"MIC OFF", "Main Mic", "Hands Free Mic", "BT Sco Mic"};

static const char *rk616_voice_call_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT"};//0-5

static const SOC_ENUM_SINGLE_DECL(rk616_playback_path_type, 0, 0, rk616_playback_path_mode);

static const SOC_ENUM_SINGLE_DECL(rk616_capture_path_type, 0, 0, rk616_capture_path_mode);

static const SOC_ENUM_SINGLE_DECL(rk616_voice_call_path_type, 0, 0, rk616_voice_call_path_mode);

static int rk616_playback_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	if (!rk616_priv) {
		printk("%s : rk616_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : playback_path = %ld\n",__func__,ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk616_priv->playback_path;

	return 0;
}

static int rk616_playback_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (!rk616_priv) {
		printk("%s : rk616_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk616_priv->playback_path == ucontrol->value.integer.value[0]){
		printk("%s : playback_path is not changed!\n",__func__);
		//return 0;
	}

	rk616_priv->playback_path = ucontrol->value.integer.value[0];

	DBG("%s : set playback_path = %ld, hdmi %s\n", __func__,
		rk616_priv->playback_path, get_hdmi_state() ? "in" : "out");

	if(get_hdmi_state())
		return 0;

	switch (rk616_priv->playback_path) {
	case OFF:
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(&codec->dapm, "Ext Spk");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case RCV:
		break;
	case SPK_PATH:
	case RING_SPK:
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin(&codec->dapm, "Ext Spk");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case HP_PATH:
	case HP_NO_MIC:
	case RING_HP:
	case RING_HP_NO_MIC:
		snd_soc_dapm_disable_pin(&codec->dapm, "Ext Spk");
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case BT:
		break;
	case SPK_HP:
	case RING_SPK_HP:
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin(&codec->dapm, "Ext Spk");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk616_capture_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	if (!rk616_priv) {
		printk("%s : rk616_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : capture_path = %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk616_priv->capture_path;

	return 0;
}

static int rk616_capture_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (!rk616_priv) {
		printk("%s : rk616_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk616_priv->capture_path == ucontrol->value.integer.value[0]){
		printk("%s : capture_path is not changed!\n", __func__);
		//return 0;
	}

	rk616_priv->capture_path = ucontrol->value.integer.value[0];

	DBG("%s : set capture_path = %ld\n", __func__, rk616_priv->capture_path);

	switch (rk616_priv->capture_path) {
	case MIC_OFF:
		snd_soc_dapm_disable_pin(&codec->dapm, "Mic Jack");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headset Jack");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case Main_Mic:
		snd_soc_dapm_enable_pin(&codec->dapm, "Mic Jack");
		snd_soc_dapm_disable_pin(&codec->dapm,"Headset Jack");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case Hands_Free_Mic:
		snd_soc_dapm_enable_pin(&codec->dapm, "Headset Jack");
		snd_soc_dapm_disable_pin(&codec->dapm, "Mic Jack");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case BT_Sco_Mic:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int rk616_voice_call_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	if (!rk616_priv) {
		printk("%s : rk616_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : playback_path = %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk616_priv->voice_call_path;

	return 0;
}

static int rk616_voice_call_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (!rk616_priv) {
		printk("%s : rk616_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk616_priv->voice_call_path == ucontrol->value.integer.value[0]){
		printk("%s : playback_path is not changed!\n",__func__);
		//return 0;
	}

	rk616_priv->voice_call_path = ucontrol->value.integer.value[0];

	DBG("%s : set playback_path = %ld\n", __func__,
		rk616_priv->voice_call_path);

	switch (rk616_priv->voice_call_path) {
	case OFF:
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(&codec->dapm, "Ext Spk");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case RCV:
		break;
	case SPK_PATH:
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin(&codec->dapm, "Ext Spk");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case HP_PATH:
	case HP_NO_MIC:
		snd_soc_dapm_disable_pin(&codec->dapm, "Ext Spk");
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_sync(&codec->dapm);
		break;
	case BT:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new rk616_snd_path_controls[] = {
	SOC_ENUM_EXT("Playback Path", rk616_playback_path_type,
		rk616_playback_path_get, rk616_playback_path_put),

	SOC_ENUM_EXT("Capture MIC Path", rk616_capture_path_type,
		rk616_capture_path_get, rk616_capture_path_put),

	SOC_ENUM_EXT("Voice Call Path", rk616_voice_call_path_type,
		rk616_voice_call_path_get, rk616_voice_call_path_put),
};

static int rk616_dacl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACL_INIT_MASK, 0);
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACL_PWRD | RK616_DACL_CLK_PWRD |
			RK616_DACL_INIT_MASK, 0);
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACL_INIT_MASK, RK616_DACL_INIT_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACL_PWRD | RK616_DACL_CLK_PWRD |
			RK616_DACL_INIT_MASK,
			RK616_DACL_PWRD | RK616_DACL_CLK_PWRD |
			RK616_DACL_INIT_WORK);
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACL_INIT_MASK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk616_dacr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACR_INIT_MASK, 0);
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACR_PWRD | RK616_DACR_CLK_PWRD |
			RK616_DACR_INIT_MASK, 0);
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACR_INIT_MASK, RK616_DACR_INIT_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACR_PWRD | RK616_DACR_CLK_PWRD |
			RK616_DACR_INIT_MASK,
			RK616_DACR_PWRD | RK616_DACR_CLK_PWRD |
			RK616_DACR_INIT_WORK);
		snd_soc_update_bits(codec, RK616_DAC_CTL,
			RK616_DACR_INIT_MASK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk616_adcl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK616_ADC_CTL,
			RK616_ADCL_CLK_PWRD | RK616_ADCL_PWRD, 0);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK616_ADC_CTL,
			RK616_ADCL_CLK_PWRD | RK616_ADCL_PWRD,
			RK616_ADCL_CLK_PWRD | RK616_ADCL_PWRD);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk616_adcr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK616_ADC_CTL,
			RK616_ADCR_CLK_PWRD | RK616_ADCR_PWRD, 0);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK616_ADC_CTL,
			RK616_ADCR_CLK_PWRD | RK616_ADCR_PWRD,
			RK616_ADCR_CLK_PWRD | RK616_ADCR_PWRD);
		break;

	default:
		return 0;
	}

	return 0;
}

/* Mixin */
static const struct snd_kcontrol_new rk616_mixinl[] = {
	SOC_DAPM_SINGLE("IN3L Switch", RK616_MIXINL_CTL,
				RK616_MIL_F_IN3L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IN1P Switch", RK616_MIXINL_CTL,
				RK616_MIL_F_IN1P_SFT, 1, 1),
	SOC_DAPM_SINGLE("MUXMIC Switch", RK616_MIXINL_CTL,
				RK616_MIL_F_MUX_SFT, 1, 1),
};

static const struct snd_kcontrol_new rk616_mixinr[] = {
	SOC_DAPM_SINGLE("MIC2N Switch", RK616_MIXINR_CTL,
				RK616_MIR_F_MIC2N_SFT, 1, 1),
	SOC_DAPM_SINGLE("IN1P Switch", RK616_MIXINR_CTL,
				RK616_MIR_F_IN1P_SFT, 1, 1),
	SOC_DAPM_SINGLE("IN3R Switch", RK616_MIXINR_CTL,
				RK616_MIR_F_IN3R_SFT, 1, 1),
	SOC_DAPM_SINGLE("MIXINR Mux Switch", RK616_MIXINR_CTL,
				RK616_MIR_F_MIRM_SFT, 1, 1),
};

/* Linemix */
static const struct snd_kcontrol_new rk616_linemix[] = {
	SOC_DAPM_SINGLE("PGAR Switch", RK616_LINEMIX_CTL,
				RK616_LM_F_PGAR_SFT, 1, 1),
	SOC_DAPM_SINGLE("PGAL Switch", RK616_LINEMIX_CTL,
				RK616_LM_F_PGAL_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACR Switch", RK616_LINEMIX_CTL,
				RK616_LM_F_DACR_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACL Switch", RK616_LINEMIX_CTL,
				RK616_LM_F_DACL_SFT, 1, 1),
};

/* HPmix */
static const struct snd_kcontrol_new rk616_hpmixl[] = {
	SOC_DAPM_SINGLE("HPMix Mux Switch", RK616_HPMIX_CTL,
				RK616_HML_F_HMM_SFT, 1, 1),
	SOC_DAPM_SINGLE("IN1P Switch", RK616_HPMIX_CTL,
				RK616_HML_F_IN1P_SFT, 1, 1),
	SOC_DAPM_SINGLE("PGAL Switch", RK616_HPMIX_CTL,
				RK616_HML_F_PGAL_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACL Switch", RK616_HPMIX_CTL,
				RK616_HML_F_DACL_SFT, 1, 1),
};

static const struct snd_kcontrol_new rk616_hpmixr[] = {
	SOC_DAPM_SINGLE("HPMix Mux Switch", RK616_HPMIX_CTL,
				RK616_HMR_F_HMM_SFT, 1, 1),
	SOC_DAPM_SINGLE("PGAR Switch", RK616_HPMIX_CTL,
				RK616_HMR_F_PGAR_SFT, 1, 1),
	SOC_DAPM_SINGLE("PGAL Switch", RK616_HPMIX_CTL,
				RK616_HMR_F_PGAL_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACR Switch", RK616_HPMIX_CTL,
				RK616_HMR_F_DACR_SFT, 1, 1),
};

/* HP MUX */
static const char *hpl_sel[] = {"HPMIXL", "DACL"};

static const struct soc_enum hpl_sel_enum =
	SOC_ENUM_SINGLE(RK616_MUXHP_HPMIX_CTL, RK616_MHL_F_SFT,
			ARRAY_SIZE(hpl_sel), hpl_sel);

static const struct snd_kcontrol_new hpl_sel_mux =
	SOC_DAPM_ENUM("HPL select Mux", hpl_sel_enum);

static const char *hpr_sel[] = {"HPMIXR", "DACR"};

static const struct soc_enum hpr_sel_enum =
	SOC_ENUM_SINGLE(RK616_MUXHP_HPMIX_CTL, RK616_MHR_F_SFT,
			ARRAY_SIZE(hpr_sel), hpr_sel);

static const struct snd_kcontrol_new hpr_sel_mux =
	SOC_DAPM_ENUM("HPR select Mux", hpr_sel_enum);

/* MIC MUX */
static const char *mic_sel[] = {"BSTL", "BSTR"};

static const struct soc_enum mic_sel_enum =
	SOC_ENUM_SINGLE(RK616_MIXINL_CTL, RK616_MM_F_SFT,
			ARRAY_SIZE(mic_sel), mic_sel);

static const struct snd_kcontrol_new mic_sel_mux =
	SOC_DAPM_ENUM("Mic select Mux", mic_sel_enum);

/* MIXINR MUX */
static const char *mixinr_sel[] = {"DIFFIN", "IN1N"};

static const struct soc_enum mixinr_sel_enum =
	SOC_ENUM_SINGLE(RK616_DIFFIN_CTL, RK616_MIRM_F_SFT,
			ARRAY_SIZE(mixinr_sel), mixinr_sel);

static const struct snd_kcontrol_new mixinr_sel_mux =
	SOC_DAPM_ENUM("Mixinr select Mux", mixinr_sel_enum);

/* HPMIX MUX */
static const char *hpmix_sel[] = {"DIFFIN", "IN1N"};

static const struct soc_enum hpmix_sel_enum =
	SOC_ENUM_SINGLE(RK616_DIFFIN_CTL, RK616_HMM_F_SFT,
			ARRAY_SIZE(hpmix_sel), hpmix_sel);

static const struct snd_kcontrol_new hpmix_sel_mux =
	SOC_DAPM_ENUM("HPMix select Mux", hpmix_sel_enum);


static const struct snd_soc_dapm_widget rk616_dapm_widgets[] = {
	/* supply */
	SND_SOC_DAPM_SUPPLY("I2S0 Interface", CRU_IO_CON0,
		3, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S1 Interface", CRU_IO_CON0,
		4, 1, NULL, 0),

	/* microphone bias */
	SND_SOC_DAPM_MICBIAS("Mic1 Bias", RK616_MICBIAS_CTL,
		RK616_MICBIAS1_PWRD_SFT, 1),
	SND_SOC_DAPM_MICBIAS("Mic2 Bias", RK616_MICBIAS_CTL,
		RK616_MICBIAS2_PWRD_SFT, 1),

	/* DACs */
	SND_SOC_DAPM_ADC_E("DACL", NULL, SND_SOC_NOPM,
		0, 0, rk616_dacl_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("DACR", NULL, SND_SOC_NOPM,
		0, 0, rk616_dacr_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* ADCs */
	SND_SOC_DAPM_ADC_E("ADCL", NULL, SND_SOC_NOPM,
		0, 0, rk616_adcl_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("ADCR", NULL, SND_SOC_NOPM,
		0, 0, rk616_adcr_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* PGA */
	SND_SOC_DAPM_PGA("BSTL", RK616_BST_CTL,
		RK616_BSTL_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("BSTR", RK616_BST_CTL,
		RK616_BSTR_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("DIFFIN", RK616_DIFFIN_CTL,
		RK616_DIFFIN_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("PGAL", RK616_PGAL_CTL,
		RK616_PGA_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("PGAR", RK616_PGAR_CTL,
		RK616_PGA_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("SPKL", RK616_SPKL_CTL,
		RK616_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("SPKR", RK616_SPKR_CTL,
		RK616_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("HPL", RK616_HPL_CTL,
		RK616_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("HPR", RK616_HPR_CTL,
		RK616_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("LINE1", RK616_LINEOUT1_CTL,
		RK616_LINEOUT_PWRD_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("LINE2", RK616_LINEOUT2_CTL,
		RK616_LINEOUT_PWRD_SFT, 1, NULL, 0),

	/* MIXER */
	SND_SOC_DAPM_MIXER("MIXINL", RK616_MIXINL_CTL,
		RK616_MIL_PWRD_SFT, 1, rk616_mixinl,
		ARRAY_SIZE(rk616_mixinl)),
	SND_SOC_DAPM_MIXER("MIXINR", RK616_MIXINR_CTL,
		RK616_MIR_PWRD_SFT, 1, rk616_mixinr,
		ARRAY_SIZE(rk616_mixinr)),
	SND_SOC_DAPM_MIXER("LINEMIX", RK616_LINEMIX_CTL,
		RK616_LM_PWRD_SFT, 1, rk616_linemix,
		ARRAY_SIZE(rk616_linemix)),
	SND_SOC_DAPM_MIXER("HPMIXL", RK616_MUXHP_HPMIX_CTL,
		RK616_HML_PWRD_SFT, 1, rk616_hpmixl,
		ARRAY_SIZE(rk616_hpmixl)),
	SND_SOC_DAPM_MIXER("HPMIXR", RK616_MUXHP_HPMIX_CTL,
		RK616_HMR_PWRD_SFT, 1, rk616_hpmixr,
		ARRAY_SIZE(rk616_hpmixr)),

	/* MUX */
	SND_SOC_DAPM_MUX("HPL Mux", SND_SOC_NOPM, 0, 0,
		&hpl_sel_mux),
	SND_SOC_DAPM_MUX("HPR Mux", SND_SOC_NOPM, 0, 0,
		&hpr_sel_mux),
	SND_SOC_DAPM_MUX("Mic Mux", SND_SOC_NOPM, 0, 0,
		&mic_sel_mux),
	SND_SOC_DAPM_MUX("MIXINR Mux", SND_SOC_NOPM, 0, 0,
		&mixinr_sel_mux),
	SND_SOC_DAPM_MUX("HPMix Mux", SND_SOC_NOPM, 0, 0,
		&hpmix_sel_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("I2S0 DAC", "HiFi Playback", 0,
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("I2S0 ADC", "HiFi Capture", 0,
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("I2S1 DAC", "Voice Playback", 0,
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("I2S1 ADC", "Voice Capture", 0,
		SND_SOC_NOPM, 0, 0),

	/* Input */
	SND_SOC_DAPM_INPUT("IN3L"),
	SND_SOC_DAPM_INPUT("IN3R"),
	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("MIC2P"),
	SND_SOC_DAPM_INPUT("MIC2N"),
	SND_SOC_DAPM_INPUT("MIC1P"),
	SND_SOC_DAPM_INPUT("MIC1N"),

	/* Output */
	SND_SOC_DAPM_OUTPUT("SPKOUTL"),
	SND_SOC_DAPM_OUTPUT("SPKOUTR"),
	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_OUTPUT("LINEOUT1"),
	SND_SOC_DAPM_OUTPUT("LINEOUT2"),
};

static const struct snd_soc_dapm_route rk616_dapm_routes[] = {
	{"I2S0 DAC", NULL, "I2S0 Interface"},
	{"I2S0 ADC", NULL, "I2S0 Interface"},
	{"I2S1 DAC", NULL, "I2S1 Interface"},
	{"I2S1 ADC", NULL, "I2S1 Interface"},

	/* Input */
	{"DIFFIN", NULL, "IN1P"},
	{"DIFFIN", NULL, "IN1N"},

	{"BSTR", NULL, "MIC2P"},
	{"BSTR", NULL, "MIC2N"},
	{"BSTL", NULL, "MIC1P"},
	{"BSTL", NULL, "MIC1N"},

	{"HPMix Mux", "DIFFIN", "DIFFIN"},
	{"HPMix Mux", "IN1N", "IN1N"},

	{"MIXINR Mux", "DIFFIN", "DIFFIN"},
	{"MIXINR Mux", "IN1N", "IN1N"},

	{"Mic Mux", "BSTR", "BSTR"},
	{"Mic Mux", "BSTL", "BSTL"},

	{"MIXINR", "MIC2N Switch", "MIC2N"},
	{"MIXINR", "IN1P Switch", "IN1P"},
	{"MIXINR", "IN3R Switch", "IN3R"},
	{"MIXINR", "MIXINR Mux Switch", "MIXINR Mux"},

	{"MIXINL", "IN3L Switch", "IN3L"},
	{"MIXINL", "IN1P Switch", "IN1P"},
	{"MIXINL", "MUXMIC Switch", "Mic Mux"},

	{"PGAR", NULL, "MIXINR"},
	{"PGAL", NULL, "MIXINL"},

	{"ADCR", NULL, "PGAR"},
	{"ADCL", NULL, "PGAL"},

	{"I2S0 ADC", NULL, "ADCR"},
	{"I2S0 ADC", NULL, "ADCL"},

	{"I2S1 ADC", NULL, "ADCR"},
	{"I2S1 ADC", NULL, "ADCL"},

	/* Output */
	{"DACR", NULL, "I2S0 DAC"},
	{"DACL", NULL, "I2S0 DAC"},

	{"DACR", NULL, "I2S1 DAC"},
	{"DACL", NULL, "I2S1 DAC"},

	{"LINEMIX", "PGAR Switch", "PGAR"},
	{"LINEMIX", "PGAL Switch", "PGAL"},
	{"LINEMIX", "DACR Switch", "DACR"},
	{"LINEMIX", "DACL Switch", "DACL"},

	{"HPMIXR", "HPMix Mux Switch", "HPMix Mux"},
	{"HPMIXR", "PGAR Switch", "PGAR"},
	{"HPMIXR", "PGAL Switch", "PGAL"},
	{"HPMIXR", "DACR Switch", "DACR"},

	{"HPMIXL", "HPMix Mux Switch", "HPMix Mux"},
	{"HPMIXL", "IN1P Switch", "PGAR"},
	{"HPMIXL", "PGAL Switch", "PGAL"},
	{"HPMIXL", "DACL Switch", "DACL"},

	{"HPR Mux", "DACR", "DACR"},
	{"HPR Mux", "HPMIXR", "HPMIXR"},
	{"HPL Mux", "DACL", "DACL"},
	{"HPL Mux", "HPMIXL", "HPMIXL"},

	{"LINE1", NULL, "LINEMIX"},
	{"LINE2", NULL, "LINEMIX"},
	{"SPKR", NULL, "HPR Mux"},
	{"SPKL", NULL, "HPL Mux"},
	{"HPR", NULL, "HPR Mux"},
	{"HPL", NULL, "HPL Mux"},

	{"LINEOUT1", NULL, "LINE1"},
	{"LINEOUT2", NULL, "LINE2"},
	{"SPKOUTR", NULL, "SPKR"},
	{"SPKOUTL", NULL, "SPKL"},
	{"HPOUTR", NULL, "HPR"},
	{"HPOUTL", NULL, "HPL"},
};

static int rk616_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		snd_soc_update_bits(codec, RK616_MICBIAS_CTL,
			RK616_MICBIAS1_PWRD | RK616_MICBIAS2_PWRD, 0);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			/* set power */
			snd_soc_update_bits(codec, RK616_PWR_ADD1, 
				RK616_ADC_PWRD | RK616_DIFFIN_MIR_PGAR_RLPWRD |
				RK616_MIC1_MIC2_MIL_PGAL_RLPWRD |
				RK616_ADCL_RLPWRD | RK616_ADCR_RLPWRD, 0);

			snd_soc_update_bits(codec, RK616_PWR_ADD2, 
				RK616_HPL_HPR_PWRD | RK616_DAC_PWRD |
				RK616_DACL_SPKL_RLPWRD | RK616_DACL_RLPWRD |
				RK616_DACR_SPKR_RLPWRD | RK616_DACR_RLPWRD |
				RK616_LM_LO_RLPWRD | RK616_HM_RLPWRD, 0);

			snd_soc_update_bits(codec, RK616_PWR_ADD3, 
				RK616_ADCL_ZO_PWRD | RK616_ADCR_ZO_PWRD |
				RK616_DACL_ZO_PWRD | RK616_DACR_ZO_PWRD,
				RK616_ADCL_ZO_PWRD | RK616_ADCR_ZO_PWRD |
				RK616_DACL_ZO_PWRD | RK616_DACR_ZO_PWRD );

			snd_soc_write(codec, RK616_MICBIAS_CTL,
				RK616_MICBIAS1_PWRD | RK616_MICBIAS2_PWRD |
				RK616_MICBIAS1_V_1_7 | RK616_MICBIAS2_V_1_7);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, RK616_PWR_ADD1, rk616_reg_defaults[RK616_PWR_ADD1]);
		snd_soc_write(codec, RK616_PWR_ADD2, rk616_reg_defaults[RK616_PWR_ADD2]);
		snd_soc_write(codec, RK616_PWR_ADD3, rk616_reg_defaults[RK616_PWR_ADD3]);
		snd_soc_update_bits(codec, RK616_MICBIAS_CTL,
			RK616_MICBIAS1_PWRD | RK616_MICBIAS2_PWRD,
			RK616_MICBIAS1_PWRD | RK616_MICBIAS2_PWRD);
		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static int rk616_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct rk616_codec_priv *rk616 = rk616_priv;

	if (!rk616) {
		printk("%s : rk616 is NULL\n", __func__);
		return -EINVAL;
	}

	rk616->stereo_sysclk = freq;

	return 0;
}

static int rk616_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adc_aif2 |= RK616_I2S_MODE_SLV;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		adc_aif2 |= RK616_I2S_MODE_MST;
		break;
	default:
		printk("%s : set master mask failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= RK616_ADC_DF_PCM;
		dac_aif1 |= RK616_DAC_DF_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= RK616_ADC_DF_I2S;
		dac_aif1 |= RK616_DAC_DF_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= RK616_ADC_DF_RJ;
		dac_aif1 |= RK616_DAC_DF_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= RK616_ADC_DF_LJ;
		dac_aif1 |= RK616_DAC_DF_LJ;
		break;
	default:
		printk("%s : set format failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		adc_aif1 |= RK616_ALRCK_POL_DIS;
		adc_aif2 |= RK616_ABCLK_POL_DIS;
		dac_aif1 |= RK616_DLRCK_POL_DIS;
		dac_aif2 |= RK616_DBCLK_POL_DIS;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		adc_aif1 |= RK616_ALRCK_POL_EN;
		adc_aif2 |= RK616_ABCLK_POL_EN;
		dac_aif1 |= RK616_DLRCK_POL_EN;
		dac_aif2 |= RK616_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		adc_aif1 |= RK616_ALRCK_POL_DIS;
		adc_aif2 |= RK616_ABCLK_POL_EN;
		dac_aif1 |= RK616_DLRCK_POL_DIS;
		dac_aif2 |= RK616_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		adc_aif1 |= RK616_ALRCK_POL_EN;
		adc_aif2 |= RK616_ABCLK_POL_DIS;
		dac_aif1 |= RK616_DLRCK_POL_EN;
		dac_aif2 |= RK616_DBCLK_POL_DIS;
		break;
	default:
		printk("%s : set dai format failed!\n", __func__);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RK616_ADC_INT_CTL1,
			RK616_ALRCK_POL_MASK | RK616_ADC_DF_MASK, adc_aif1);
	snd_soc_update_bits(codec, RK616_ADC_INT_CTL2,
			RK616_ABCLK_POL_MASK | RK616_I2S_MODE_MASK, adc_aif2);
	snd_soc_update_bits(codec, RK616_DAC_INT_CTL1,
			RK616_DLRCK_POL_MASK | RK616_DAC_DF_MASK, dac_aif1);
	snd_soc_update_bits(codec, RK616_DAC_INT_CTL2,
			RK616_DBCLK_POL_MASK, dac_aif2);

	return 0;
}

static int rk616_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec =rtd->codec;
	struct rk616_codec_priv *rk616 = rk616_priv;
	unsigned int rate = params_rate(params);
	unsigned int div;
	unsigned int adc_aif1 = 0, adc_aif2  = 0, dac_aif1 = 0, dac_aif2  = 0;
	u32 mfd_aif1 = 0, mfd_aif2 = 0, mfd_i2s_ctl = 0;

	if (!rk616) {
		printk("%s : rk616 is NULL\n", __func__);
		return -EINVAL;
	}

	// bclk = codec_clk / 4
	// lrck = bclk / (wl * 2)
	div = (((rk616->stereo_sysclk / 4) / rate) / 2);

	if ((rk616->stereo_sysclk % (4 * rate * 2) > 0) ||
	    (div != 16 && div != 20 && div != 24 && div != 32)) {
		printk("%s : need PLL\n", __func__);
		return -EINVAL;
	}

	switch (div) {
	case 16:
		adc_aif2 |= RK616_ADC_WL_16;
		dac_aif2 |= RK616_DAC_WL_16;
		break;
	case 20:
		adc_aif2 |= RK616_ADC_WL_20;
		dac_aif2 |= RK616_DAC_WL_20;
		break;
	case 24:
		adc_aif2 |= RK616_ADC_WL_24;
		dac_aif2 |= RK616_DAC_WL_24;
		break;
	case 32:
		adc_aif2 |= RK616_ADC_WL_32;
		dac_aif2 |= RK616_DAC_WL_32;
		break;
	default:
		return -EINVAL;
	}


	DBG("%s : MCLK = %dHz, sample rate = %dHz, div = %d\n", __func__,
		rk616->stereo_sysclk, rate, div);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= RK616_ADC_VWL_16;
		dac_aif1 |= RK616_DAC_VWL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= RK616_ADC_VWL_20;
		dac_aif1 |= RK616_DAC_VWL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= RK616_ADC_VWL_24;
		dac_aif1 |= RK616_DAC_VWL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= RK616_ADC_VWL_32;
		dac_aif1 |= RK616_DAC_VWL_32;
		break;
	default:
		return -EINVAL;
	}

	/*switch (params_channels(params)) {
	case RK616_MONO:
		adc_aif1 |= RK616_ADC_TYPE_MONO;
		break;
	case RK616_STEREO:
		adc_aif1 |= RK616_ADC_TYPE_STEREO;
		break;
	default:
		return -EINVAL;
	}*/

	//MIC1N/P and MIC2N/P can only line to ADCL, so set mono type.
	adc_aif1 |= RK616_ADC_TYPE_MONO;

	adc_aif1 |= RK616_ADC_SWAP_DIS;
	adc_aif2 |= RK616_ADC_RST_DIS;
	dac_aif1 |= RK616_DAC_SWAP_DIS;
	dac_aif2 |= RK616_DAC_RST_DIS;

	rk616->rate = rate;

	snd_soc_update_bits(codec, RK616_ADC_INT_CTL1,
			 RK616_ADC_VWL_MASK | RK616_ADC_SWAP_MASK |
			 RK616_ADC_TYPE_MASK, adc_aif1);
	snd_soc_update_bits(codec, RK616_ADC_INT_CTL2,
			RK616_ADC_WL_MASK | RK616_ADC_RST_MASK, adc_aif2);
	snd_soc_update_bits(codec, RK616_DAC_INT_CTL1,
			 RK616_DAC_VWL_MASK | RK616_DAC_SWAP_MASK, dac_aif1);
	snd_soc_update_bits(codec, RK616_DAC_INT_CTL2,
			RK616_DAC_WL_MASK | RK616_DAC_RST_MASK, dac_aif2);

	switch (dai->id) {
	case RK616_HIFI:
		mfd_aif1 |= I2S1_OUT_DISABLE | I2S0_PD_DISABLE;
		mfd_aif2 |= I2S0_SI_EN;
		mfd_i2s_ctl |= 0;
		break;
	case RK616_VOICE:
		mfd_aif1 |= I2S0_OUT_DISABLE | I2S1_PD_DISABLE;
		mfd_aif2 |= I2S1_SI_EN;
		mfd_i2s_ctl |= I2S_CHANNEL_SEL | PCM_TO_I2S_MUX;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, CRU_IO_CON0,
			I2S1_OUT_DISABLE | I2S0_OUT_DISABLE |
			I2S1_PD_DISABLE | I2S0_PD_DISABLE, mfd_aif1);
	snd_soc_update_bits(codec, CRU_IO_CON1,
			I2S1_SI_EN | I2S0_SI_EN, mfd_aif2);
	snd_soc_update_bits(codec, CRU_PCM2IS2_CON2,
			APS_SEL | APS_CLR | I2S_CHANNEL_SEL,
			mfd_i2s_ctl);
	return 0;
}

static int rk616_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int is_spk_pd, is_hp_pd;

	is_spk_pd = RK616_PWRD & snd_soc_read(codec, RK616_SPKL_CTL);
	is_spk_pd &= RK616_PWRD & snd_soc_read(codec, RK616_SPKR_CTL);

	is_hp_pd = RK616_PWRD & snd_soc_read(codec, RK616_HPL_CTL);
	is_hp_pd &= RK616_PWRD & snd_soc_read(codec, RK616_HPR_CTL);

	if (mute) {
		if (rk616_priv && rk616_priv->spk_ctl_gpio != INVALID_GPIO &&
		    !is_spk_pd) {
			DBG("%s : set spk ctl gpio LOW\n", __func__);
			gpio_set_value(rk616_priv->spk_ctl_gpio, GPIO_LOW);
		}

		if (rk616_priv && rk616_priv->hp_ctl_gpio != INVALID_GPIO &&
		    !is_hp_pd) {
			DBG("%s : set hp ctl gpio LOW\n", __func__);
			gpio_set_value(rk616_priv->hp_ctl_gpio, GPIO_LOW);
			snd_soc_write(codec, RK616_CLK_CHPUMP, 0x41);
		}
	} else {
		if (rk616_priv && rk616_priv->hp_ctl_gpio != INVALID_GPIO &&
		    !is_hp_pd) {
			snd_soc_write(codec, RK616_CLK_CHPUMP, 0x21);
			msleep(10);
			DBG("%s : set hp ctl gpio HIGH\n", __func__);
			gpio_set_value(rk616_priv->hp_ctl_gpio, GPIO_HIGH);
		}

		if (rk616_priv && rk616_priv->spk_ctl_gpio != INVALID_GPIO &&
		    !is_spk_pd) {
			DBG("%s : set spk ctl gpio HIGH\n", __func__);
			gpio_set_value(rk616_priv->spk_ctl_gpio, GPIO_HIGH);
		}
	}

	return 0;
}

static struct rk616_reg_val_typ palyback_power_up_list[] = {
	{0x804, 0x46}, //DAC DSM, 0x06: x1, 0x26: x1.25, 0x46: x1.5, 0x66: x1.75
	{0x868, 0x02}, //power up
	{0x86c, 0x0f}, //DACL/R UN INIT
	{0x86c, 0x00}, //DACL/R and DACL/R CLK power up
	{0x86c, 0x30}, //DACL/R INIT
	{0x874, 0x14}, //Mux HPMIXR from HPMIXR(bit 0), Mux HPMIXL from HPMIXL(bit 1),HPMIXL/R power up
	{0x878, 0xee}, //HPMIXL/HPMIXR from DACL/DACR(bit 4, bit 0)
	{0x88c, 3<<5|SPKOUT_VOLUME},//power up SPKOUTL (bit 7), volume (bit 0-4)
	{0x890, 3<<5|SPKOUT_VOLUME}, //power up SPKOUTR (bit 7), volume (bit 0-4)
	{0x88c, SPKOUT_VOLUME}, //unmute SPKOUTL (bit 5), volume (bit 0-4)
	{0x890, SPKOUT_VOLUME}, //unmute SPKOUTR (bit 5), volume (bit 0-4)
	{0x894, 3<<5|HPOUT_VOLUME}, //power up HPOUTL (bit 7), volume (bit 0-4)
	{0x898, 3<<5|HPOUT_VOLUME}, //power up HPOUTR (bit 7), volume (bit 0-4)
	{0x894, HPOUT_VOLUME}, //unmute HPOUTL (bit 5), volume (bit 0-4)
	{0x898, HPOUT_VOLUME}, //unmute HPOUTR (bit 5), volume (bit 0-4)
};
#define RK616_CODEC_PALYBACK_POWER_UP_LIST_LEN ARRAY_SIZE(palyback_power_up_list)

static struct rk616_reg_val_typ palyback_power_down_list[] = {
	{0x898, 0xe0}, //mute HPOUTR (bit 5), volume (bit 0-4)
	{0x894, 0xe0}, //mute HPOUTL (bit 5), volume (bit 0-4)
	{0x890, 0xe0}, //mute SPKOUTR (bit 5), volume (bit 0-4)
	{0x88c, 0xe0}, //mute SPKOUTL (bit 5), volume (bit 0-4)
	{0x878, 0xff}, //HPMIXL/HPMIXR from DACL/DACR(bit 4, bit 0)
	{0x874, 0x3c}, //Power down HPMIXL/R
	{0x86c, 0x3f}, //DACL/R INIT
	{0x868, 0xff}, //power down
};
#define RK616_CODEC_PALYBACK_POWER_DOWN_LIST_LEN ARRAY_SIZE(palyback_power_down_list)

static struct rk616_reg_val_typ capture_power_up_list[] = {
	{0x828, 0x09}, //Set for Capture pop noise
	{0x83c, 0x00}, //power up
	{0x840, 0x69}, //BST_L power up, unmute, and Single-Ended(bit 6), volume 0-20dB(bit 5)
	{0x848, 0x06}, //MIXINL power up and unmute, MININL from MICMUX, MICMUX from BST_L
	{0x84c, 0x24}, //MIXINL from MIXMUX volume (bit 3-5)
	{0x860, 0x0c}, //PGAL power up unmute,volume (bit 0-4)
	{0x89c, 0x7f}, //MICBIAS1 power up (bit 7, Vout = 1.7 * Vref(1.65V) = 2.8V (bit 3-5)
	{0x8a8, 0x09}, //ADCL/R power, and clear ADCL/R buf
	{0x8a8, 0x00}, //ADCL/R power, and clear ADCL/R buf
	{0x8c4, 0x57}, //L mod time
	//{0x904, 0x57}, //R mod time
	{0x828, 0x39}, //Set for Capture pop noise && enable agc control pga
	{0x8a4, 0x03}, //enable cross zero detect
	{0x8c0, 0x00}, //L agc choice mod 2
	//{0x900, 0x00}, //R agc choice mod 2
	{0x8d4, 0x13}, //max low
	{0x8d8, 0x08}, //max high
	{0x8dc, 0xc2}, //min low
	{0x8e0, 0x16}, //min high
	{0x8e4, 0x70}, //L enable agc
	//{0x924, 0x70}, //R enbale agc
};
#define RK616_CODEC_CAPTURE_POWER_UP_LIST_LEN ARRAY_SIZE(capture_power_up_list)

static struct rk616_reg_val_typ capture_power_down_list[] = {
	{0x8a8, 0x3f}, //ADCL/R power down, and clear ADCL/R buf
	{0x89c, 0xff}, //MICBIAS1 power down (bit 7, Vout = 1.7 * Vref(1.65V) = 2.8V (bit 3-5)
	{0x860, 0xcc}, //PGAL power down ,mute,volume 0dB(bit 0-4)
	{0x84c, 0x24}, //MIXINL from MIXMUX volume 0dB(bit 3-5)
	{0x848, 0x1f}, //MIXINL power down and mute, MININL No selecting, MICMUX from BST_L
	{0x840, 0x99}, //BST_L power down, mute, and Single-Ended(bit 6), volume 0(bit 5)
	{0x83c, 0x7c}, //power down
	{0x8e4, 0x38}, //L disable agc
	//{0x924, 0x38}, //R disbale agc
	{0x8c4, 0x25},
	//{0x904, 0x25},
	{0x828, 0x09},
	{0x8a4, 0x0F},
	{0x8c0, 0x10},
	//{0x900, 0x10},
	{0x8d4, 0x26},
	{0x8d8, 0x40},
	{0x8dc, 0x36},
	{0x8e0, 0x20},
};
#define RK616_CODEC_CAPTURE_POWER_DOWN_LIST_LEN ARRAY_SIZE(capture_power_down_list)

static int rk616_codec_power_up(int type)
{
	struct snd_soc_codec *codec = rk616_priv->codec;
	int i;

	if (!rk616_priv || !rk616_priv->codec) {
		printk("%s : rk616_priv or rk616_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	printk("%s : power up %s%s\n", __func__,
		type == RK616_CODEC_PALYBACK ? "playback" : "",
		type == RK616_CODEC_CAPTURE ? "capture" : "");

	if (type == RK616_CODEC_PALYBACK) {
		for (i = 0; i < RK616_CODEC_PALYBACK_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, palyback_power_up_list[i].reg,
				palyback_power_up_list[i].value);
		}
		codec_set_spk(!get_hdmi_state());
	} else if (type == RK616_CODEC_CAPTURE) {
		for (i = 0; i < RK616_CODEC_CAPTURE_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_up_list[i].reg,
				capture_power_up_list[i].value);
		}
	}

	return 0;
}

static int rk616_codec_power_down(int type)
{
	struct rk616_codec_priv *rk616 = rk616_priv;
	struct snd_soc_codec *codec = rk616_priv->codec;
	int i;
      
	if (!rk616_priv || !rk616_priv->codec) {
		printk("%s : rk616_priv or rk616_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk616->playback_active <= 0 && rk616->capture_active <= 0)
		type = RK616_CODEC_ALL;

	printk("%s : power down %s%s%s\n", __func__,
		type == RK616_CODEC_PALYBACK ? "playback" : "",
		type == RK616_CODEC_CAPTURE ? "capture" : "",
		type == RK616_CODEC_ALL ? "all" : "");

	if (type == RK616_CODEC_CAPTURE) {
		for (i = 0; i < RK616_CODEC_CAPTURE_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_down_list[i].reg,
				capture_power_down_list[i].value);
		}
	} else if (type == RK616_CODEC_PALYBACK) {
		for (i = 0; i < RK616_CODEC_PALYBACK_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, palyback_power_down_list[i].reg,
				palyback_power_down_list[i].value);
		}
	} else if (type == RK616_CODEC_ALL) {
		rk616_reset(codec);
	}

	return 0;
}

static void  rk616_codec_capture_work(struct work_struct *work)
{
	DBG("%s : rk616_codec_work_capture_type = %d\n", __func__,
		rk616_codec_work_capture_type);

	switch (rk616_codec_work_capture_type) {
	case RK616_CODEC_WORK_POWER_DOWN:
		rk616_codec_power_down(RK616_CODEC_CAPTURE);
		break;
	case RK616_CODEC_WORK_POWER_UP:
		rk616_codec_power_up(RK616_CODEC_CAPTURE);
		break;
	default:
		break;
	}

	rk616_codec_work_capture_type = RK616_CODEC_WORK_NULL;
}

static int rk616_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct rk616_codec_priv *rk616 = rk616_priv;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	bool is_codec_playback_running = rk616->playback_active > 0 ;
	bool is_codec_capture_running = rk616->capture_active > 0;

	if (!rk616_for_mid)
	{
		DBG("%s immediately return for phone\n",__func__);
		return 0;
	}

	if (!rk616) {
		printk("%s : rk616 is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : substream->stream : %s \n", __func__,
		playback ? "PLAYBACK":"CAPTURE");

	if (playback)
		rk616->playback_active++;
	else
		rk616->capture_active++;

	if (playback) {
		if (rk616->playback_active > 0) {
			if (!is_codec_playback_running)
				rk616_codec_power_up(RK616_CODEC_PALYBACK);
			else
				DBG(" Warning : playback has been opened, so return! \n");
		}
	} else {//capture
		if (rk616->capture_active > 0 && !is_codec_capture_running) {
			if (rk616_codec_work_capture_type != RK616_CODEC_WORK_POWER_UP) {
				cancel_delayed_work_sync(&capture_delayed_work);
				if (rk616_codec_work_capture_type == RK616_CODEC_WORK_NULL) {
					rk616_codec_power_up(RK616_CODEC_CAPTURE);
				} else {
					DBG(" Warning : capture being closed, so interrupt the shutdown process ! \n");
					rk616_codec_work_capture_type = RK616_CODEC_WORK_NULL;
				}
			} else {
				DBG("Warning : capture being opened, so return ! \n");
			}
		}
	}

	return 0;
}

static void rk616_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct rk616_codec_priv *rk616 = rk616_priv;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	bool is_codec_playback_running = rk616->playback_active > 0;
	bool is_codec_capture_running = rk616->capture_active > 0;
	
	if (!rk616_for_mid)
	{
		DBG("%s immediately return for phone\n", __func__);
		return;
	}

	if (!rk616) {
		printk("%s : rk616 is NULL\n", __func__);
		return;
	}

	DBG("%s : substream->stream : %s \n", __func__,
		playback ? "PLAYBACK":"CAPTURE");

	if (playback)
		rk616->playback_active--;
	else
		rk616->capture_active--;

	if (playback) {
		if (rk616->playback_active <= 0) {
			if (is_codec_playback_running == true)
				rk616_codec_power_down(RK616_CODEC_PALYBACK);
			else
				DBG(" Warning : playback has been closed, so return !\n");
		}
	} else {//capture
		if (rk616->capture_active <= 0) {
			if ((rk616_codec_work_capture_type != RK616_CODEC_WORK_POWER_DOWN) &&
			    (is_codec_capture_running == true)) {
				cancel_delayed_work_sync(&capture_delayed_work);
				/*
				* If rk616_codec_work_capture_type is NULL means codec already power down,
				* so power up codec.
				* If rk616_codec_work_capture_type is RK616_CODEC_WORK_POWER_UP it means
				* codec haven't be powered up, so we don't need to power down codec.
				* If is playback call power down, power down immediatly, because audioflinger
				* already has delay 3s.
				*/
				if (rk616_codec_work_capture_type == RK616_CODEC_WORK_NULL) {
					rk616_codec_work_capture_type = RK616_CODEC_WORK_POWER_DOWN;
					queue_delayed_work(rk616_codec_workq, &capture_delayed_work,msecs_to_jiffies(3000));
				} else {
					rk616_codec_work_capture_type = RK616_CODEC_WORK_NULL;
					DBG(" Warning : capture being opened, so interrupt the open process ! \n");
				}
			} else {
				DBG(" Warning : capture has been closed or it being closed, so return !\n");
			}
		}
	}

}

#define RK616_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK616_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK616_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops rk616_dai_ops = {
	.hw_params	= rk616_hw_params,
	.set_fmt	= rk616_set_dai_fmt,
	.set_sysclk	= rk616_set_dai_sysclk,
	.digital_mute	= rk616_digital_mute,
	.startup	= rk616_startup,
	.shutdown	= rk616_shutdown,
};

static struct snd_soc_dai_driver rk616_dai[] = {
	{
		.name = "rk616-hifi",
		.id = RK616_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK616_PLAYBACK_RATES,
			.formats = RK616_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK616_CAPTURE_RATES,
			.formats = RK616_FORMATS,
		},
		.ops = &rk616_dai_ops,
	},
	{
		.name = "rk616-voice",
		.id = RK616_VOICE,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK616_PLAYBACK_RATES,
			.formats = RK616_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK616_CAPTURE_RATES,
			.formats = RK616_FORMATS,
		},
		.ops = &rk616_dai_ops,
	},

};

static int rk616_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	if (rk616_for_mid)
	{
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk616_codec_work_capture_type != RK616_CODEC_WORK_NULL) {
			rk616_codec_work_capture_type = RK616_CODEC_WORK_NULL;
		}
		rk616_codec_power_down(RK616_CODEC_ALL);
	}
	else
		rk616_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int rk616_resume(struct snd_soc_codec *codec)
{
	if (!rk616_for_mid)
		rk616_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

#ifdef RK616_PROC	
static int rk616_proc_init(void);
#endif

static int rk616_probe(struct snd_soc_codec *codec)
{
	struct rk616_codec_priv *rk616;
	int ret;
	unsigned int val;

	DBG("%s\n", __func__);

	rk616 = kzalloc(sizeof(struct rk616_codec_priv), GFP_KERNEL);
	if (!rk616) {
		printk("%s : rk616 priv kzalloc failed!\n", __func__);
		return -ENOMEM;
	}

	rk616->codec = codec;

	if (rk616_mfd && rk616_mfd->pdata && rk616_mfd->pdata->spk_ctl_gpio) {
		gpio_request(rk616_mfd->pdata->spk_ctl_gpio, NULL);
		gpio_direction_output(rk616_mfd->pdata->spk_ctl_gpio, GPIO_LOW);
		rk616->spk_ctl_gpio = rk616_mfd->pdata->spk_ctl_gpio;
	} else {
		printk("%s : rk616 or pdata or spk_ctl_gpio is NULL!\n", __func__);
		rk616->spk_ctl_gpio = INVALID_GPIO;
	}

	if (rk616_mfd && rk616_mfd->pdata && rk616_mfd->pdata->hp_ctl_gpio) {
		gpio_request(rk616_mfd->pdata->hp_ctl_gpio, NULL);
		gpio_direction_output(rk616_mfd->pdata->hp_ctl_gpio, GPIO_LOW);
		rk616->hp_ctl_gpio = rk616_mfd->pdata->hp_ctl_gpio;
	} else {
		printk("%s : rk616 or pdata or hp_ctl_gpio is NULL!\n", __func__);
		rk616->hp_ctl_gpio = INVALID_GPIO;
	}

	if (rk616_for_mid)
	{
		rk616->playback_active = 0;
		rk616->capture_active = 0;

		rk616_codec_workq = create_freezable_workqueue("rk616-codec");

		if (rk616_codec_workq == NULL) {
			printk("%s : create work FAIL! rk616_codec_workq is NULL!\n", __func__);
			ret = -ENOMEM;
			goto err__;
		}
	}

	rk616_priv = rk616;

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		printk("%s : Failed to set cache I/O: %d\n", __func__, ret);
		goto err__;
	}

	codec->hw_read = rk616_codec_read;
	codec->hw_write = (hw_write_t)rk616_hw_write;
	codec->read = rk616_codec_read;
	codec->write = rk616_codec_write;

	val = snd_soc_read(codec, RK616_RESET);
	if (val != rk616_reg_defaults[RK616_RESET]) {
		printk("%s : codec register 0: %x is not a 0x00000003\n", __func__, val);
		ret = -ENODEV;
		goto err__;
	}

	rk616_reset(codec);

	if (!rk616_for_mid)
	{
		codec->dapm.bias_level = SND_SOC_BIAS_OFF;
		rk616_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	}
#ifdef RK616_PROC
	rk616_proc_init();
#endif
	return 0;

err__:
	kfree(rk616);
	rk616 = NULL;
	rk616_priv = NULL;

	return ret;
}

/* power down chip */
static int rk616_remove(struct snd_soc_codec *codec)
{
	DBG("%s\n", __func__);

	if (!rk616_priv) {
		printk("%s : rk616_priv is NULL\n", __func__);
		return 0;
	}

	if (rk616_priv->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk616_priv->spk_ctl_gpio, GPIO_LOW);

	if (rk616_priv->hp_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk616_priv->hp_ctl_gpio, GPIO_LOW);

	mdelay(10);

	if (rk616_for_mid)
	{
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk616_codec_work_capture_type != RK616_CODEC_WORK_NULL) {
			rk616_codec_work_capture_type = RK616_CODEC_WORK_NULL;
		}
	}

	snd_soc_write(codec, RK616_RESET, 0xfc);
	mdelay(10);
	snd_soc_write(codec, RK616_RESET, 0x3);
	mdelay(10);

	if (rk616_priv)
		kfree(rk616_priv);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_rk616 = {
	.probe =	rk616_probe,
	.remove =	rk616_remove,
	.suspend =	rk616_suspend,
	.resume =	rk616_resume,
	.set_bias_level = rk616_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(rk616_reg_defaults),
	.reg_word_size = sizeof(unsigned int),
	.reg_cache_default = rk616_reg_defaults,
	.volatile_register = rk616_volatile_register,
	.readable_register = rk616_codec_register,
	.controls = rk616_snd_controls,
	.num_controls = ARRAY_SIZE(rk616_snd_controls),
	.dapm_widgets = rk616_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk616_dapm_widgets),
	.dapm_routes = rk616_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rk616_dapm_routes),
};

static __devinit int rk616_platform_probe(struct platform_device *pdev)
{
	struct mfd_rk616 *rk616 = dev_get_drvdata(pdev->dev.parent);

	DBG("%s\n", __func__);

	if (!rk616) {
		printk("%s : rk616 is NULL\n", __func__);
		return -EINVAL;
	}

	rk616_mfd = rk616;

	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_rk616, rk616_dai, ARRAY_SIZE(rk616_dai));
}

static __devexit int rk616_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

void rk616_platform_shutdown(struct platform_device *pdev)
{
	DBG("%s\n", __func__);

	if (!rk616_priv || !rk616_priv->codec) {
		printk("%s : rk616_priv or rk616_priv->codec is NULL\n", __func__);
		return;
	}

	if (rk616_priv->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk616_priv->spk_ctl_gpio, GPIO_LOW);

	if (rk616_priv->hp_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk616_priv->hp_ctl_gpio, GPIO_LOW);

	mdelay(10);

	if (rk616_for_mid) {
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk616_codec_work_capture_type != RK616_CODEC_WORK_NULL) {
			rk616_codec_work_capture_type = RK616_CODEC_WORK_NULL;
		}
	}

	snd_soc_write(rk616_priv->codec, RK616_RESET, 0xfc);
	mdelay(10);
	snd_soc_write(rk616_priv->codec, RK616_RESET, 0x3);

	if (rk616_priv)
		kfree(rk616_priv);
}

static struct platform_driver rk616_codec_driver = {
	.driver = {
		   .name = "rk616-codec",
		   .owner = THIS_MODULE,
		   },
	.probe = rk616_platform_probe,
	.remove = __devexit_p(rk616_platform_remove),
	.shutdown = rk616_platform_shutdown,
};


static __init int rk616_modinit(void)
{
	return platform_driver_register(&rk616_codec_driver);
}
module_init(rk616_modinit);

static __exit void rk616_exit(void)
{
	platform_driver_unregister(&rk616_codec_driver);
}
module_exit(rk616_exit);

#ifdef RK616_PROC
static ssize_t rk616_proc_write(struct file *file, const char __user *buffer,
			   unsigned long len, void *data)
{
	struct snd_soc_codec *codec = rk616_priv->codec;
	char *cookie_pot, *p, debug_write_read = 0;
	int reg, value;

	if (!rk616_priv || !rk616_priv->codec) {
		printk("%s : rk616_priv or rk616_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	cookie_pot = (char *)vmalloc( len );
	if (!cookie_pot) {
		return -ENOMEM;
	} else {
		if (copy_from_user( cookie_pot, buffer, len )) 
			return -EFAULT;
	}

	switch(cookie_pot[0]) {
	case 'd':
	case 'D':
		debug_write_read ++;
		debug_write_read %= 2;
		if (debug_write_read != 0)
			printk("Debug read and write reg on\n");
		else	
			printk("Debug read and write reg off\n");	
		break;	
	case 'r':
	case 'R':
		printk("Read reg debug\n");		
		if (cookie_pot[1] ==':') {
			debug_write_read = 1;
			strsep(&cookie_pot,":");
			while ((p = strsep(&cookie_pot, ","))) {
				reg = simple_strtol(p, NULL, 16);
				value = snd_soc_read(codec, reg);
				if (value <= 0xffff)
					printk("rk616_read:0x%04x = 0x%04x\n", reg, value);
			}
			debug_write_read = 0;
			printk("\n");
		} else {
			printk("Error Read reg debug.\n");
			printk("For example: echo r:22,23,24,25>rk616_ts\n");
		}
		break;
	case 'w':
	case 'W':
		printk("Write reg debug\n");		
		if (cookie_pot[1] ==':') {
			debug_write_read = 1;
			strsep(&cookie_pot, ":");
			while ((p = strsep(&cookie_pot, "="))) {
				reg = simple_strtol(p, NULL, 16);
				p=strsep(&cookie_pot, ",");
				value = simple_strtol(p, NULL, 16);
				snd_soc_write(codec, reg, value);
				printk("rk616_write:0x%04x = 0x%04x\n", reg, value);
			}
			debug_write_read = 0;
			printk("\n");
		} else {
			printk("Error Write reg debug.\n");
			printk("For example: w:22=0,23=0,24=0,25=0>rk616_ts\n");
		}
		break;	
	case 'a':
		printk("Dump reg \n");		

		for (reg = 0; reg < 0x6e; reg += 2) {
			value = snd_soc_read(codec, reg);
			printk("rk616_read : 0x%04x = 0x%04x\n", reg, value);
		}
		break;
	default:
		printk("Help for rk616_ts .\n-->The Cmd list: \n");
		printk("-->'d&&D' Open or Off the debug\n");
		printk("-->'r&&R' Read reg debug,Example: echo 'r:22,23,24,25'>rk616_ts\n");
		printk("-->'w&&W' Write reg debug,Example: echo 'w:22=0,23=0,24=0,25=0'>rk616_ts\n");
		break;
	}

	return len;
}

static const struct file_operations rk616_proc_fops = {
	.owner		= THIS_MODULE,
};

static int rk616_proc_init(void)
{
	struct proc_dir_entry *rk616_proc_entry;

	rk616_proc_entry = create_proc_entry("driver/rk616_ts", 0777, NULL);

	if(rk616_proc_entry != NULL) {
		rk616_proc_entry->write_proc = rk616_proc_write;
		return 0;
	} else {
		printk("create proc error !\n");
		return -1;
	}
}
#endif

MODULE_DESCRIPTION("ASoC RK616 driver");
MODULE_AUTHOR("chenjq <chenjq@rock-chips.com>");
MODULE_LICENSE("GPL");
