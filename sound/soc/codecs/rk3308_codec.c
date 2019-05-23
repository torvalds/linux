/*
 * rk3308_codec.c -- RK3308 ALSA Soc Audio Driver
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/rockchip/grf.h>
#include <linux/version.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/simple_card.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rk3308_codec.h"
#include "rk3308_codec_provider.h"

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#define CODEC_DRV_NAME			"rk3308-acodec"

#define ADC_GRP_SKIP_MAGIC		0x1001
#define ADC_LR_GROUP_MAX		4
#define ADC_STABLE_MS			200
#define DEBUG_POP_ALWAYS		0
#define HPDET_POLL_MS			2000
#define NOT_USED			255
#define LOOPBACK_HANDLE_MS		100
#define PA_DRV_MS		        5

#define GRF_SOC_CON1			0x304
#define GRF_CHIP_ID			0x800
#define GRF_I2S2_8CH_SDI_SFT		0
#define GRF_I2S3_4CH_SDI_SFT		8
#define GRF_I2S1_2CH_SDI_SFT		12

#define GRF_I2S2_8CH_SDI_R_MSK(i, v)	((v >> (i * 2 + GRF_I2S2_8CH_SDI_SFT)) & 0x3)
#define GRF_I2S2_8CH_SDI_W_MSK(i)	(0x3 << (i * 2 + GRF_I2S2_8CH_SDI_SFT + 16))
#define GRF_I2S2_8CH_SDI(i, v)		(((v & 0x3) << (i * 2 + GRF_I2S2_8CH_SDI_SFT)) |\
					 GRF_I2S2_8CH_SDI_W_MSK(i))

#define GRF_I2S3_4CH_SDI_W_MSK(i)	(0x3 << (i * 2 + GRF_I2S3_4CH_SDI_SFT + 16))
#define GRF_I2S3_4CH_SDI(i, v)		(((v & 0x3) << (i * 2 + GRF_I2S3_4CH_SDI_SFT)) |\
					 GRF_I2S3_4CH_SDI_W_MSK(i))

#define GRF_I2S1_2CH_SDI_W_MSK		(0x3 << (GRF_I2S1_2CH_SDI_SFT + 16))
#define GRF_I2S1_2CH_SDI(v)		(((v & 0x3) << GRF_I2S1_2CH_SDI_SFT) |\
					 GRF_I2S1_2CH_SDI_W_MSK)

#define DETECT_GRF_ACODEC_HPDET_COUNTER		0x0030
#define DETECT_GRF_ACODEC_HPDET_CON		0x0034
#define DETECT_GRF_ACODEC_HPDET_STATUS		0x0038
#define DETECT_GRF_ACODEC_HPDET_STATUS_CLR	0x003c

/* 200ms based on pclk is 100MHz */
#define DEFAULT_HPDET_COUNT			20000000
#define HPDET_NEG_IRQ_SFT			1
#define HPDET_POS_IRQ_SFT			0
#define HPDET_BOTH_NEG_POS			((1 << HPDET_NEG_IRQ_SFT) |\
						 (1 << HPDET_POS_IRQ_SFT))

#define ACODEC_VERSION_A			0xa
#define ACODEC_VERSION_B			0xb

enum {
	ACODEC_TO_I2S2_8CH = 0,
	ACODEC_TO_I2S3_4CH,
	ACODEC_TO_I2S1_2CH,
};

enum {
	ADC_GRP0_MICIN = 0,
	ADC_GRP0_LINEIN
};

enum {
	ADC_TYPE_NORMAL = 0,
	ADC_TYPE_LOOPBACK,
	ADC_TYPE_DBG,
	ADC_TYPE_ALL,
};

enum {
	DAC_LINEOUT = 0,
	DAC_HPOUT = 1,
	DAC_LINEOUT_HPOUT = 11,
};

enum {
	EXT_MICBIAS_NONE = 0,
	EXT_MICBIAS_FUNC1,  /* enable external micbias via GPIO */
	EXT_MICBIAS_FUNC2,  /* enable external micbias via regulator */
};

enum {
	PATH_IDLE = 0,
	PATH_BUSY,
};

enum {
	PM_NORMAL = 0,
	PM_LLP_DOWN,		/* light low power down */
	PM_LLP_UP,
	PM_DLP_DOWN,		/* deep low power down */
	PM_DLP_UP,
	PM_DLP_DOWN2,
	PM_DLP_UP2,
};

struct rk3308_codec_priv {
	const struct device *plat_dev;
	struct device dev;
	struct reset_control *reset;
	struct regmap *regmap;
	struct regmap *grf;
	struct regmap *detect_grf;
	struct clk *pclk;
	struct clk *mclk_rx;
	struct clk *mclk_tx;
	struct gpio_desc *micbias_en_gpio;
	struct gpio_desc *hp_ctl_gpio;
	struct gpio_desc *spk_ctl_gpio;
	struct gpio_desc *pa_drv_gpio;
	struct snd_soc_codec *codec;
	struct snd_soc_jack *hpdet_jack;
	struct regulator *vcc_micbias;
	u32 codec_ver;

	/*
	 * To select ADCs for groups:
	 *
	 * grp 0 -- select ADC1 / ADC2
	 * grp 1 -- select ADC3 / ADC4
	 * grp 2 -- select ADC5 / ADC6
	 * grp 3 -- select ADC7 / ADC8
	 */
	u32 used_adc_grps;
	/* The ADC group which is used for loop back */
	u32 loopback_grp;
	u32 cur_dbg_grp;
	u32 en_always_grps[ADC_LR_GROUP_MAX];
	u32 en_always_grps_num;
	u32 skip_grps[ADC_LR_GROUP_MAX];
	u32 i2s_sdis[ADC_LR_GROUP_MAX];
	u32 to_i2s_grps;
	u32 delay_loopback_handle_ms;
	u32 delay_start_play_ms;
	u32 delay_pa_drv_ms;
	u32 micbias_num;
	u32 micbias_volt;
	int which_i2s;
	int irq;
	int adc_grp0_using_linein;
	int adc_zerocross;
	/* 0: line out, 1: hp out, 11: lineout and hpout */
	int dac_output;
	int dac_path_state;

	int ext_micbias;
	int pm_state;

	/* AGC L/R Off/on */
	unsigned int agc_l[ADC_LR_GROUP_MAX];
	unsigned int agc_r[ADC_LR_GROUP_MAX];

	/* AGC L/R Approximate Sample Rate */
	unsigned int agc_asr_l[ADC_LR_GROUP_MAX];
	unsigned int agc_asr_r[ADC_LR_GROUP_MAX];

	/* ADC MIC Mute/Work */
	unsigned int mic_mute_l[ADC_LR_GROUP_MAX];
	unsigned int mic_mute_r[ADC_LR_GROUP_MAX];

	/* For the high pass filter */
	unsigned int hpf_cutoff[ADC_LR_GROUP_MAX];

	/* Only hpout do fade-in and fade-out */
	unsigned int hpout_l_dgain;
	unsigned int hpout_r_dgain;

	bool adc_grps_endisable[ADC_LR_GROUP_MAX];
	bool dac_endisable;
	bool enable_all_adcs;
	bool enable_micbias;
	bool micbias1;
	bool micbias2;
	bool hp_jack_reversed;
	bool hp_plugged;
	bool loopback_dacs_enabled;
	bool no_deep_low_power;
	bool no_hp_det;
	struct delayed_work hpdet_work;
	struct delayed_work loopback_work;

#if defined(CONFIG_DEBUG_FS)
	struct dentry *dbg_codec;
#endif
};

static const DECLARE_TLV_DB_SCALE(rk3308_codec_alc_agc_grp_gain_tlv,
				  -1800, 150, 2850);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_alc_agc_grp_max_gain_tlv,
				  -1350, 600, 2850);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_alc_agc_grp_min_gain_tlv,
				  -1800, 600, 2400);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_adc_alc_gain_tlv,
				  -1800, 150, 2850);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_lineout_gain_tlv,
				  -600, 150, 0);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_hpout_gain_tlv,
				  -3900, 150, 600);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_hpmix_gain_tlv,
				  -600, 600, 0);

static const DECLARE_TLV_DB_RANGE(rk3308_codec_adc_mic_gain_tlv_a,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(2000, 0, 0),
);

static const DECLARE_TLV_DB_RANGE(rk3308_codec_adc_mic_gain_tlv_b,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(660, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(1300, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(2000, 0, 0),
);

static bool handle_loopback(struct rk3308_codec_priv *rk3308);

static int check_micbias(int micbias);

static int rk3308_codec_micbias_enable(struct rk3308_codec_priv *rk3308,
				       int micbias);
static int rk3308_codec_micbias_disable(struct rk3308_codec_priv *rk3308);

static int rk3308_codec_hpout_l_get_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_hpout_l_put_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_hpout_r_get_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_hpout_r_put_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_hpf_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_hpf_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_agc_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_agc_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_agc_asr_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_agc_asr_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_mic_mute_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_mic_mute_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_mic_gain_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_mic_gain_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_micbias_volts_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_micbias_volts_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_main_micbias_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol);
static int rk3308_codec_main_micbias_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol);

static const char *offon_text[2] = {
	[0] = "Off",
	[1] = "On",
};

static const char *mute_text[2] = {
	[0] = "Work",
	[1] = "Mute",
};

/* ADC MICBIAS Volt */
#define MICBIAS_VOLT_NUM		8

#define MICBIAS_VREFx0_5		0
#define MICBIAS_VREFx0_55		1
#define MICBIAS_VREFx0_6		2
#define MICBIAS_VREFx0_65		3
#define MICBIAS_VREFx0_7		4
#define MICBIAS_VREFx0_75		5
#define MICBIAS_VREFx0_8		6
#define MICBIAS_VREFx0_85		7

static const char *micbias_volts_enum_array[MICBIAS_VOLT_NUM] = {
	[MICBIAS_VREFx0_5] = "VREFx0_5",
	[MICBIAS_VREFx0_55] = "VREFx0_55",
	[MICBIAS_VREFx0_6] = "VREFx0_6",
	[MICBIAS_VREFx0_65] = "VREFx0_65",
	[MICBIAS_VREFx0_7] = "VREFx0_7",
	[MICBIAS_VREFx0_75] = "VREFx0_75",
	[MICBIAS_VREFx0_8] = "VREFx0_8",
	[MICBIAS_VREFx0_85] = "VREFx0_85",
};

static const struct soc_enum rk3308_micbias_volts_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(micbias_volts_enum_array), micbias_volts_enum_array),
};

/* ADC MICBIAS1 and MICBIAS2 Main Switch */
static const struct soc_enum rk3308_main_micbias_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(offon_text), offon_text),
};

static const struct soc_enum rk3308_hpf_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(1, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(2, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(3, 0, ARRAY_SIZE(offon_text), offon_text),
};

/* ALC AGC Switch */
static const struct soc_enum rk3308_agc_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(1, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(1, 1, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(2, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(2, 1, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(3, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(3, 1, ARRAY_SIZE(offon_text), offon_text),
};

/* ADC MIC Mute/Work Switch */
static const struct soc_enum rk3308_mic_mute_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(1, 0, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(1, 1, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(2, 0, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(2, 1, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(3, 0, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(3, 1, ARRAY_SIZE(mute_text), mute_text),
};

/* ALC AGC Approximate Sample Rate */
#define AGC_ASR_NUM				8

#define AGC_ASR_96KHZ				0
#define AGC_ASR_48KHZ				1
#define AGC_ASR_44_1KHZ				2
#define AGC_ASR_32KHZ				3
#define AGC_ASR_24KHZ				4
#define AGC_ASR_16KHZ				5
#define AGC_ASR_12KHZ				6
#define AGC_ASR_8KHZ				7

static const char *agc_asr_text[AGC_ASR_NUM] = {
	[AGC_ASR_96KHZ] = "96KHz",
	[AGC_ASR_48KHZ] = "48KHz",
	[AGC_ASR_44_1KHZ] = "44.1KHz",
	[AGC_ASR_32KHZ] = "32KHz",
	[AGC_ASR_24KHZ] = "24KHz",
	[AGC_ASR_16KHZ] = "16KHz",
	[AGC_ASR_12KHZ] = "12KHz",
	[AGC_ASR_8KHZ] = "8KHz",
};

static const struct soc_enum rk3308_agc_asr_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(1, 0, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(1, 1, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(2, 0, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(2, 1, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(3, 0, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(3, 1, ARRAY_SIZE(agc_asr_text), agc_asr_text),
};

static const struct snd_kcontrol_new mic_gains_a[] = {
	/* ADC MIC */
	SOC_SINGLE_EXT_TLV("ADC MIC Group 0 Left Volume",
			   RK3308_ADC_ANA_CON01(0),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 0 Right Volume",
			   RK3308_ADC_ANA_CON01(0),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 1 Left Volume",
			   RK3308_ADC_ANA_CON01(1),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 1 Right Volume",
			   RK3308_ADC_ANA_CON01(1),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 2 Left Volume",
			   RK3308_ADC_ANA_CON01(2),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 2 Right Volume",
			   RK3308_ADC_ANA_CON01(2),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 3 Left Volume",
			   RK3308_ADC_ANA_CON01(3),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 3 Right Volume",
			   RK3308_ADC_ANA_CON01(3),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_a),
};

static const struct snd_kcontrol_new mic_gains_b[] = {
	/* ADC MIC */
	SOC_SINGLE_EXT_TLV("ADC MIC Group 0 Left Volume",
			   RK3308_ADC_ANA_CON01(0),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 0 Right Volume",
			   RK3308_ADC_ANA_CON01(0),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 1 Left Volume",
			   RK3308_ADC_ANA_CON01(1),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 1 Right Volume",
			   RK3308_ADC_ANA_CON01(1),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 2 Left Volume",
			   RK3308_ADC_ANA_CON01(2),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 2 Right Volume",
			   RK3308_ADC_ANA_CON01(2),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 3 Left Volume",
			   RK3308_ADC_ANA_CON01(3),
			   RK3308_ADC_CH1_MIC_GAIN_SFT,
			   RK3308_ADC_CH1_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
	SOC_SINGLE_EXT_TLV("ADC MIC Group 3 Right Volume",
			   RK3308_ADC_ANA_CON01(3),
			   RK3308_ADC_CH2_MIC_GAIN_SFT,
			   RK3308_ADC_CH2_MIC_GAIN_MAX,
			   0,
			   rk3308_codec_mic_gain_get,
			   rk3308_codec_mic_gain_put,
			   rk3308_codec_adc_mic_gain_tlv_b),
};

static const struct snd_kcontrol_new rk3308_codec_dapm_controls[] = {
	/* ALC AGC Group */
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Left Volume",
			     RK3308_ALC_L_DIG_CON03(0),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Right Volume",
			     RK3308_ALC_R_DIG_CON03(0),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Left Volume",
			     RK3308_ALC_L_DIG_CON03(1),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Right Volume",
			     RK3308_ALC_R_DIG_CON03(1),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Left Volume",
			     RK3308_ALC_L_DIG_CON03(2),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Right Volume",
			     RK3308_ALC_R_DIG_CON03(2),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Left Volume",
			     RK3308_ALC_L_DIG_CON03(3),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Right Volume",
			     RK3308_ALC_R_DIG_CON03(3),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	/* ALC AGC MAX */
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(0),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(0),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(1),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(1),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(2),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(2),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(3),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(3),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	/* ALC AGC MIN */
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(0),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(0),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(1),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(1),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(2),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(2),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(3),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(3),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	/* ALC AGC Switch */
	SOC_ENUM_EXT("ALC AGC Group 0 Left Switch", rk3308_agc_enum_array[0],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Group 0 Right Switch", rk3308_agc_enum_array[1],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Group 1 Left Switch", rk3308_agc_enum_array[2],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Group 1 Right Switch", rk3308_agc_enum_array[3],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Group 2 Left Switch", rk3308_agc_enum_array[4],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Group 2 Right Switch", rk3308_agc_enum_array[5],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Group 3 Left Switch", rk3308_agc_enum_array[6],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Group 3 Right Switch", rk3308_agc_enum_array[7],
		     rk3308_codec_agc_get, rk3308_codec_agc_put),

	/* ALC AGC Approximate Sample Rate */
	SOC_ENUM_EXT("AGC Group 0 Left Approximate Sample Rate", rk3308_agc_asr_enum_array[0],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Group 0 Right Approximate Sample Rate", rk3308_agc_asr_enum_array[1],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Group 1 Left Approximate Sample Rate", rk3308_agc_asr_enum_array[2],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Group 1 Right Approximate Sample Rate", rk3308_agc_asr_enum_array[3],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Group 2 Left Approximate Sample Rate", rk3308_agc_asr_enum_array[4],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Group 2 Right Approximate Sample Rate", rk3308_agc_asr_enum_array[5],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Group 3 Left Approximate Sample Rate", rk3308_agc_asr_enum_array[6],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Group 3 Right Approximate Sample Rate", rk3308_agc_asr_enum_array[7],
		     rk3308_codec_agc_asr_get, rk3308_codec_agc_asr_put),

	/* ADC MICBIAS Voltage */
	SOC_ENUM_EXT("ADC MICBIAS Voltage", rk3308_micbias_volts_enum_array[0],
		     rk3308_codec_micbias_volts_get, rk3308_codec_micbias_volts_put),

	/* ADC Main MICBIAS Switch */
	SOC_ENUM_EXT("ADC Main MICBIAS", rk3308_main_micbias_enum_array[0],
		     rk3308_codec_main_micbias_get, rk3308_codec_main_micbias_put),

	/* ADC MICBIAS1 and MICBIAS2 Switch */
	SOC_SINGLE("ADC MICBIAS1", RK3308_ADC_ANA_CON07(1),
		   RK3308_ADC_MIC_BIAS_BUF_SFT, 1, 0),
	SOC_SINGLE("ADC MICBIAS2", RK3308_ADC_ANA_CON07(2),
		   RK3308_ADC_MIC_BIAS_BUF_SFT, 1, 0),

	/* ADC MIC Mute/Work Switch */
	SOC_ENUM_EXT("ADC MIC Group 0 Left Switch", rk3308_mic_mute_enum_array[0],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Group 0 Right Switch", rk3308_mic_mute_enum_array[1],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Group 1 Left Switch", rk3308_mic_mute_enum_array[2],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Group 1 Right Switch", rk3308_mic_mute_enum_array[3],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Group 2 Left Switch", rk3308_mic_mute_enum_array[4],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Group 2 Right Switch", rk3308_mic_mute_enum_array[5],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Group 3 Left Switch", rk3308_mic_mute_enum_array[6],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Group 3 Right Switch", rk3308_mic_mute_enum_array[7],
		     rk3308_codec_mic_mute_get, rk3308_codec_mic_mute_put),

	/* ADC ALC */
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 0 Left Volume",
			     RK3308_ADC_ANA_CON03(0),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 0 Right Volume",
			     RK3308_ADC_ANA_CON04(0),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 1 Left Volume",
			     RK3308_ADC_ANA_CON03(1),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 1 Right Volume",
			     RK3308_ADC_ANA_CON04(1),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 2 Left Volume",
			     RK3308_ADC_ANA_CON03(2),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 2 Right Volume",
			     RK3308_ADC_ANA_CON04(2),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 3 Left Volume",
			     RK3308_ADC_ANA_CON03(3),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 3 Right Volume",
			     RK3308_ADC_ANA_CON04(3),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),

	/* ADC High Pass Filter */
	SOC_ENUM_EXT("ADC Group 0 HPF Cut-off", rk3308_hpf_enum_array[0],
		     rk3308_codec_hpf_get, rk3308_codec_hpf_put),
	SOC_ENUM_EXT("ADC Group 1 HPF Cut-off", rk3308_hpf_enum_array[1],
		     rk3308_codec_hpf_get, rk3308_codec_hpf_put),
	SOC_ENUM_EXT("ADC Group 2 HPF Cut-off", rk3308_hpf_enum_array[2],
		     rk3308_codec_hpf_get, rk3308_codec_hpf_put),
	SOC_ENUM_EXT("ADC Group 3 HPF Cut-off", rk3308_hpf_enum_array[3],
		     rk3308_codec_hpf_get, rk3308_codec_hpf_put),

	/* DAC LINEOUT */
	SOC_SINGLE_TLV("DAC LINEOUT Left Volume",
		       RK3308_DAC_ANA_CON04,
		       RK3308_DAC_L_LINEOUT_GAIN_SFT,
		       RK3308_DAC_L_LINEOUT_GAIN_MAX,
		       0, rk3308_codec_dac_lineout_gain_tlv),
	SOC_SINGLE_TLV("DAC LINEOUT Right Volume",
		       RK3308_DAC_ANA_CON04,
		       RK3308_DAC_R_LINEOUT_GAIN_SFT,
		       RK3308_DAC_R_LINEOUT_GAIN_MAX,
		       0, rk3308_codec_dac_lineout_gain_tlv),

	/* DAC HPOUT */
	SOC_SINGLE_EXT_TLV("DAC HPOUT Left Volume",
			   RK3308_DAC_ANA_CON05,
			   RK3308_DAC_L_HPOUT_GAIN_SFT,
			   RK3308_DAC_L_HPOUT_GAIN_MAX,
			   0,
			   rk3308_codec_hpout_l_get_tlv,
			   rk3308_codec_hpout_l_put_tlv,
			   rk3308_codec_dac_hpout_gain_tlv),
	SOC_SINGLE_EXT_TLV("DAC HPOUT Right Volume",
			   RK3308_DAC_ANA_CON06,
			   RK3308_DAC_R_HPOUT_GAIN_SFT,
			   RK3308_DAC_R_HPOUT_GAIN_MAX,
			   0,
			   rk3308_codec_hpout_r_get_tlv,
			   rk3308_codec_hpout_r_put_tlv,
			   rk3308_codec_dac_hpout_gain_tlv),

	/* DAC HPMIX */
	SOC_SINGLE_RANGE_TLV("DAC HPMIX Left Volume",
			     RK3308_DAC_ANA_CON12,
			     RK3308_DAC_L_HPMIX_GAIN_SFT,
			     RK3308_DAC_L_HPMIX_GAIN_MIN,
			     RK3308_DAC_L_HPMIX_GAIN_MAX,
			     0, rk3308_codec_dac_hpmix_gain_tlv),
	SOC_SINGLE_RANGE_TLV("DAC HPMIX Right Volume",
			     RK3308_DAC_ANA_CON12,
			     RK3308_DAC_R_HPMIX_GAIN_SFT,
			     RK3308_DAC_R_HPMIX_GAIN_MIN,
			     RK3308_DAC_R_HPMIX_GAIN_MAX,
			     0, rk3308_codec_dac_hpmix_gain_tlv),
};

static int rk3308_codec_agc_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	if (e->shift_l)
		ucontrol->value.integer.value[0] = rk3308->agc_r[e->reg];
	else
		ucontrol->value.integer.value[0] = rk3308->agc_l[e->reg];

	return 0;
}

static int rk3308_codec_agc_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value = ucontrol->value.integer.value[0];
	int grp = e->reg;

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	if (value) {
		/* ALC AGC On */
		if (e->shift_l) {
			/* ALC AGC Right On */
			regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON09(grp),
					   RK3308_AGC_FUNC_SEL_MSK,
					   RK3308_AGC_FUNC_SEL_EN);
			regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON11(grp),
					   RK3308_ADC_ALCR_CON_GAIN_PGAR_MSK,
					   RK3308_ADC_ALCR_CON_GAIN_PGAR_EN);

			rk3308->agc_r[e->reg] = 1;
		} else {
			/* ALC AGC Left On */
			regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON09(grp),
					   RK3308_AGC_FUNC_SEL_MSK,
					   RK3308_AGC_FUNC_SEL_EN);
			regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON11(grp),
					   RK3308_ADC_ALCL_CON_GAIN_PGAL_MSK,
					   RK3308_ADC_ALCL_CON_GAIN_PGAL_EN);

			rk3308->agc_l[e->reg] = 1;
		}
	} else {
		/* ALC AGC Off */
		if (e->shift_l) {
			/* ALC AGC Right Off */
			regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON09(grp),
					   RK3308_AGC_FUNC_SEL_MSK,
					   RK3308_AGC_FUNC_SEL_DIS);
			regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON11(grp),
					   RK3308_ADC_ALCR_CON_GAIN_PGAR_MSK,
					   RK3308_ADC_ALCR_CON_GAIN_PGAR_DIS);

			rk3308->agc_r[e->reg] = 0;
		} else {
			/* ALC AGC Left Off */
			regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON09(grp),
					   RK3308_AGC_FUNC_SEL_MSK,
					   RK3308_AGC_FUNC_SEL_DIS);
			regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON11(grp),
					   RK3308_ADC_ALCL_CON_GAIN_PGAL_MSK,
					   RK3308_ADC_ALCL_CON_GAIN_PGAL_DIS);

			rk3308->agc_l[e->reg] = 0;
		}
	}

	return 0;
}

static int rk3308_codec_agc_asr_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;
	int grp = e->reg;

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	if (e->shift_l) {
		regmap_read(rk3308->regmap, RK3308_ALC_R_DIG_CON04(grp), &value);
		rk3308->agc_asr_r[e->reg] = value >> RK3308_AGC_APPROX_RATE_SFT;
		ucontrol->value.integer.value[0] = rk3308->agc_asr_r[e->reg];
	} else {
		regmap_read(rk3308->regmap, RK3308_ALC_L_DIG_CON04(grp), &value);
		rk3308->agc_asr_l[e->reg] = value >> RK3308_AGC_APPROX_RATE_SFT;
		ucontrol->value.integer.value[0] = rk3308->agc_asr_l[e->reg];
	}

	return 0;
}

static int rk3308_codec_agc_asr_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;
	int grp = e->reg;

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	value = ucontrol->value.integer.value[0] << RK3308_AGC_APPROX_RATE_SFT;

	if (e->shift_l) {
		/* ALC AGC Right Approximate Sample Rate */
		regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON04(grp),
				   RK3308_AGC_APPROX_RATE_MSK,
				   value);
		rk3308->agc_asr_r[e->reg] = ucontrol->value.integer.value[0];
	} else {
		/* ALC AGC Left Approximate Sample Rate */
		regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON04(grp),
				   RK3308_AGC_APPROX_RATE_MSK,
				   value);
		rk3308->agc_asr_l[e->reg] = ucontrol->value.integer.value[0];
	}

	return 0;
}

static int rk3308_codec_mic_mute_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;
	int grp = e->reg;

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	if (e->shift_l) {
		/* ADC MIC Right Mute/Work Infos */
		regmap_read(rk3308->regmap, RK3308_ADC_DIG_CON03(grp), &value);
		rk3308->mic_mute_r[e->reg] = (value & RK3308_ADC_R_CH_BIST_SINE) >>
					     RK3308_ADC_R_CH_BIST_SFT;
		ucontrol->value.integer.value[0] = rk3308->mic_mute_r[e->reg];
	} else {
		/* ADC MIC Left Mute/Work Infos */
		regmap_read(rk3308->regmap, RK3308_ADC_DIG_CON03(grp), &value);
		rk3308->mic_mute_l[e->reg] = (value & RK3308_ADC_L_CH_BIST_SINE) >>
					     RK3308_ADC_L_CH_BIST_SFT;
		ucontrol->value.integer.value[0] = rk3308->mic_mute_l[e->reg];
	}

	return 0;
}

static int rk3308_codec_mic_mute_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;
	int grp = e->reg;

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	if (e->shift_l) {
		/* ADC MIC Right Mute/Work Configuration */
		value = ucontrol->value.integer.value[0] << RK3308_ADC_R_CH_BIST_SFT;
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON03(grp),
				   RK3308_ADC_R_CH_BIST_SINE,
				   value);
		rk3308->mic_mute_r[e->reg] = ucontrol->value.integer.value[0];
	} else {
		/* ADC MIC Left Mute/Work Configuration */
		value = ucontrol->value.integer.value[0] << RK3308_ADC_L_CH_BIST_SFT;
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON03(grp),
				   RK3308_ADC_L_CH_BIST_SINE,
				   value);
		rk3308->mic_mute_l[e->reg] = ucontrol->value.integer.value[0];
	}

	return 0;
}

static int rk3308_codec_micbias_volts_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rk3308->micbias_volt;

	return 0;
}

static int rk3308_codec_micbias_volts_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int volt = ucontrol->value.integer.value[0];
	int ret;

	ret = check_micbias(volt);
	if (ret < 0) {
		dev_err(rk3308->plat_dev, "The invalid micbias volt: %d\n",
			volt);
		return ret;
	}

	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(0),
			   RK3308_ADC_LEVEL_RANGE_MICBIAS_MSK,
			   volt);

	rk3308->micbias_volt = volt;

	return 0;
}

static int rk3308_codec_main_micbias_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rk3308->enable_micbias;

	return 0;
}

static int rk3308_codec_main_micbias_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int on = ucontrol->value.integer.value[0];

	if (on) {
		if (!rk3308->enable_micbias)
			rk3308_codec_micbias_enable(rk3308, rk3308->micbias_volt);
	} else {
		if (rk3308->enable_micbias)
			rk3308_codec_micbias_disable(rk3308);
	}

	return 0;
}

static int rk3308_codec_mic_gain_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_get_volsw_range(kcontrol, ucontrol);
}

static int rk3308_codec_mic_gain_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int gain = ucontrol->value.integer.value[0];

	if (gain > RK3308_ADC_CH1_MIC_GAIN_MAX) {
		dev_err(rk3308->plat_dev, "%s: invalid mic gain: %d\n",
			__func__, gain);
		return -EINVAL;
	}

	if (rk3308->codec_ver == ACODEC_VERSION_A) {
		/*
		 * From the TRM, there are only suupport 0dB(gain==0) and
		 * 20dB(gain==3) on the codec version A.
		 */
		if (!(gain == 0 || gain == RK3308_ADC_CH1_MIC_GAIN_MAX)) {
			dev_err(rk3308->plat_dev,
				"version A doesn't supported: %d, expect: 0,%d\n",
				gain, RK3308_ADC_CH1_MIC_GAIN_MAX);
			return 0;
		}
	}

	return snd_soc_put_volsw_range(kcontrol, ucontrol);
}

static int rk3308_codec_hpf_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	regmap_read(rk3308->regmap, RK3308_ADC_DIG_CON04(e->reg), &value);
	if (value & RK3308_ADC_HPF_PATH_MSK)
		rk3308->hpf_cutoff[e->reg] = 0;
	else
		rk3308->hpf_cutoff[e->reg] = 1;

	ucontrol->value.integer.value[0] = rk3308->hpf_cutoff[e->reg];

	return 0;
}

static int rk3308_codec_hpf_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value = ucontrol->value.integer.value[0];

	if (e->reg < 0 || e->reg > ADC_LR_GROUP_MAX - 1) {
		dev_err(rk3308->plat_dev,
			"%s: Invalid ADC grp: %d\n", __func__, e->reg);
		return -EINVAL;
	}

	if (value) {
		/* Enable high pass filter for ADCs */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON04(e->reg),
				   RK3308_ADC_HPF_PATH_MSK,
				   RK3308_ADC_HPF_PATH_EN);
	} else {
		/* Disable high pass filter for ADCs. */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON04(e->reg),
				   RK3308_ADC_HPF_PATH_MSK,
				   RK3308_ADC_HPF_PATH_DIS);
	}

	rk3308->hpf_cutoff[e->reg] = value;

	return 0;
}

static int rk3308_codec_hpout_l_get_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_get_volsw_range(kcontrol, ucontrol);
}

static int rk3308_codec_hpout_l_put_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int dgain = ucontrol->value.integer.value[0];

	if (dgain > RK3308_DAC_L_HPOUT_GAIN_MAX) {
		dev_err(rk3308->plat_dev, "%s: invalid l_dgain: %d\n",
			__func__, dgain);
		return -EINVAL;
	}

	rk3308->hpout_l_dgain = dgain;

	return snd_soc_put_volsw_range(kcontrol, ucontrol);
}

static int rk3308_codec_hpout_r_get_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_get_volsw_range(kcontrol, ucontrol);
}

static int rk3308_codec_hpout_r_put_tlv(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int dgain = ucontrol->value.integer.value[0];

	if (dgain > RK3308_DAC_R_HPOUT_GAIN_MAX) {
		dev_err(rk3308->plat_dev, "%s: invalid r_dgain: %d\n",
			__func__, dgain);
		return -EINVAL;
	}

	rk3308->hpout_r_dgain = dgain;

	return snd_soc_put_volsw_range(kcontrol, ucontrol);
}

static u32 to_mapped_grp(struct rk3308_codec_priv *rk3308, int idx)
{
	return rk3308->i2s_sdis[idx];
}

static bool adc_for_each_grp(struct rk3308_codec_priv *rk3308,
			     int type, int idx, u32 *grp)
{
	if (type == ADC_TYPE_NORMAL) {
		u32 mapped_grp = to_mapped_grp(rk3308, idx);
		int max_grps;

		if (rk3308->enable_all_adcs)
			max_grps = ADC_LR_GROUP_MAX;
		else
			max_grps = rk3308->used_adc_grps;

		if (idx >= max_grps)
			return false;

		if ((!rk3308->loopback_dacs_enabled) &&
		    handle_loopback(rk3308) &&
		    rk3308->loopback_grp == mapped_grp) {
			/*
			 * Ths loopback DACs are closed, and specify the
			 * loopback ADCs.
			 */
			*grp = ADC_GRP_SKIP_MAGIC;
		} else if (rk3308->en_always_grps_num &&
			   rk3308->skip_grps[mapped_grp]) {
			/* To set the skip flag if the ADC GRP is enabled. */
			*grp = ADC_GRP_SKIP_MAGIC;
		} else {
			*grp = mapped_grp;
		}

		dev_dbg(rk3308->plat_dev,
			"ADC_TYPE_NORMAL, idx: %d, mapped_grp: %d, get grp: %d,\n",
			idx, mapped_grp, *grp);
	} else if (type == ADC_TYPE_ALL) {
		if (idx >= ADC_LR_GROUP_MAX)
			return false;

		*grp = idx;
		dev_dbg(rk3308->plat_dev,
			"ADC_TYPE_ALL, idx: %d, get grp: %d\n",
			idx, *grp);
	} else if (type == ADC_TYPE_DBG) {
		if (idx >= ADC_LR_GROUP_MAX)
			return false;

		if (idx == (int)rk3308->cur_dbg_grp)
			*grp = idx;
		else
			*grp = ADC_GRP_SKIP_MAGIC;

		dev_dbg(rk3308->plat_dev,
			"ADC_TYPE_DBG, idx: %d, get grp: %d\n",
			idx, *grp);
	} else {
		if (idx >= 1)
			return false;

		*grp = rk3308->loopback_grp;
		dev_dbg(rk3308->plat_dev,
			"ADC_TYPE_LOOPBACK, idx: %d, get grp: %d\n",
			idx, *grp);
	}

	return true;
}

static int rk3308_codec_get_dac_path_state(struct rk3308_codec_priv *rk3308)
{
	return rk3308->dac_path_state;
}

static void rk3308_codec_set_dac_path_state(struct rk3308_codec_priv *rk3308,
					    int state)
{
	rk3308->dac_path_state = state;
}

static void rk3308_headphone_ctl(struct rk3308_codec_priv *rk3308, int on)
{
	if (rk3308->hp_ctl_gpio)
		gpiod_direction_output(rk3308->hp_ctl_gpio, on);
}

static void rk3308_speaker_ctl(struct rk3308_codec_priv *rk3308, int on)
{
	if (on) {
		if (rk3308->pa_drv_gpio) {
			gpiod_direction_output(rk3308->pa_drv_gpio, on);
			msleep(rk3308->delay_pa_drv_ms);
		}

		if (rk3308->spk_ctl_gpio)
			gpiod_direction_output(rk3308->spk_ctl_gpio, on);
	} else {
		if (rk3308->spk_ctl_gpio)
			gpiod_direction_output(rk3308->spk_ctl_gpio, on);

		if (rk3308->pa_drv_gpio) {
			msleep(rk3308->delay_pa_drv_ms);
			gpiod_direction_output(rk3308->pa_drv_gpio, on);
		}
	}
}

static int rk3308_codec_reset(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	reset_control_assert(rk3308->reset);
	usleep_range(2000, 2500);	/* estimated value */
	reset_control_deassert(rk3308->reset);

	regmap_write(rk3308->regmap, RK3308_GLB_CON, 0x00);
	usleep_range(200, 300);		/* estimated value */
	regmap_write(rk3308->regmap, RK3308_GLB_CON,
		     RK3308_SYS_WORK |
		     RK3308_DAC_DIG_WORK |
		     RK3308_ADC_DIG_WORK);

	return 0;
}

static int rk3308_codec_adc_dig_reset(struct rk3308_codec_priv *rk3308)
{
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_ADC_DIG_WORK,
			   RK3308_ADC_DIG_RESET);
	udelay(50);
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_ADC_DIG_WORK,
			   RK3308_ADC_DIG_WORK);

	return 0;
}

static int rk3308_codec_dac_dig_reset(struct rk3308_codec_priv *rk3308)
{
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_DAC_DIG_WORK,
			   RK3308_DAC_DIG_RESET);
	udelay(50);
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_DAC_DIG_WORK,
			   RK3308_DAC_DIG_WORK);

	return 0;
}

static int rk3308_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		regcache_cache_only(rk3308->regmap, false);
		regcache_sync(rk3308->regmap);
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}

	return 0;
}

static int rk3308_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;
	int idx, grp, is_master;
	int type = ADC_TYPE_ALL;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adc_aif2 |= RK3308_ADC_IO_MODE_SLAVE;
		adc_aif2 |= RK3308_ADC_MODE_SLAVE;
		dac_aif2 |= RK3308_DAC_IO_MODE_SLAVE;
		dac_aif2 |= RK3308_DAC_MODE_SLAVE;
		is_master = 0;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		adc_aif2 |= RK3308_ADC_IO_MODE_MASTER;
		adc_aif2 |= RK3308_ADC_MODE_MASTER;
		dac_aif2 |= RK3308_DAC_IO_MODE_MASTER;
		dac_aif2 |= RK3308_DAC_MODE_MASTER;
		is_master = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= RK3308_ADC_I2S_MODE_PCM;
		dac_aif1 |= RK3308_DAC_I2S_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= RK3308_ADC_I2S_MODE_I2S;
		dac_aif1 |= RK3308_DAC_I2S_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= RK3308_ADC_I2S_MODE_RJ;
		dac_aif1 |= RK3308_DAC_I2S_MODE_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= RK3308_ADC_I2S_MODE_LJ;
		dac_aif1 |= RK3308_DAC_I2S_MODE_LJ;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_NORMAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_NORMAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_NORMAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_REVERSAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_REVERSAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_REVERSAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_REVERSAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_NORMAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_REVERSAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_NORMAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_REVERSAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_REVERSAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_NORMAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_REVERSAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Hold ADC Digital registers start at master mode
	 *
	 * There are 8 ADCs and use the same SCLK and LRCK internal for master
	 * mode, We need to make sure that they are in effect at the same time,
	 * otherwise they will cause the abnormal clocks.
	 */
	if (is_master)
		regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
				   RK3308_ADC_DIG_WORK,
				   RK3308_ADC_DIG_RESET);

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON01(grp),
				   RK3308_ADC_I2S_LRC_POL_MSK |
				   RK3308_ADC_I2S_MODE_MSK,
				   adc_aif1);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON02(grp),
				   RK3308_ADC_IO_MODE_MSK |
				   RK3308_ADC_MODE_MSK |
				   RK3308_ADC_I2S_BIT_CLK_POL_MSK,
				   adc_aif2);
	}

	/* Hold ADC Digital registers end at master mode */
	if (is_master)
		regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
				   RK3308_ADC_DIG_WORK,
				   RK3308_ADC_DIG_WORK);

	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON01,
			   RK3308_DAC_I2S_LRC_POL_MSK |
			   RK3308_DAC_I2S_MODE_MSK,
			   dac_aif1);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON02,
			   RK3308_DAC_IO_MODE_MSK |
			   RK3308_DAC_MODE_MSK |
			   RK3308_DAC_I2S_BIT_CLK_POL_MSK,
			   dac_aif2);

	return 0;
}

static int rk3308_codec_dac_dig_config(struct rk3308_codec_priv *rk3308,
				       struct snd_pcm_hw_params *params)
{
	unsigned int dac_aif1 = 0, dac_aif2 = 0;

	/* Clear the status of DAC DIG Digital reigisters */
	rk3308_codec_dac_dig_reset(rk3308);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_32BITS;
		break;
	default:
		return -EINVAL;
	}

	dac_aif1 |= RK3308_DAC_I2S_LR_NORMAL;
	dac_aif2 |= RK3308_DAC_I2S_WORK;

	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON01,
			   RK3308_DAC_I2S_VALID_LEN_MSK |
			   RK3308_DAC_I2S_LR_MSK,
			   dac_aif1);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON02,
			   RK3308_DAC_I2S_MSK,
			   dac_aif2);

	return 0;
}

static int rk3308_codec_adc_dig_config(struct rk3308_codec_priv *rk3308,
				       struct snd_pcm_hw_params *params)
{
	unsigned int adc_aif1 = 0, adc_aif2 = 0;
	int type = ADC_TYPE_NORMAL;
	int idx, grp;

	/* Clear the status of ADC DIG Digital reigisters */
	rk3308_codec_adc_dig_reset(rk3308);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_32BITS;
		break;
	default:
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case 1:
		adc_aif1 |= RK3308_ADC_I2S_MONO;
		break;
	case 2:
	case 4:
	case 6:
	case 8:
		adc_aif1 |= RK3308_ADC_I2S_STEREO;
		break;
	default:
		return -EINVAL;
	}

	adc_aif1 |= RK3308_ADC_I2S_LR_NORMAL;
	adc_aif2 |= RK3308_ADC_I2S_WORK;

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON01(grp),
				   RK3308_ADC_I2S_VALID_LEN_MSK |
				   RK3308_ADC_I2S_LR_MSK |
				   RK3308_ADC_I2S_TYPE_MSK,
				   adc_aif1);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON02(grp),
				   RK3308_ADC_I2S_MSK,
				   adc_aif2);
	}

	return 0;
}

static int rk3308_codec_update_adc_grps(struct rk3308_codec_priv *rk3308,
					struct snd_pcm_hw_params *params)
{
	switch (params_channels(params)) {
	case 1:
		rk3308->used_adc_grps = 1;
		break;
	case 2:
	case 4:
	case 6:
	case 8:
		rk3308->used_adc_grps = params_channels(params) / 2;
		break;
	default:
		dev_err(rk3308->plat_dev, "Invalid channels: %d\n",
			params_channels(params));
		return -EINVAL;
	}

	return 0;
}

static int rk3308_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		int dgain;

		if (mute) {
			for (dgain = 0x2; dgain <= 0x7; dgain++) {
				/*
				 * Keep the max -> min digital CIC interpolation
				 * filter gain step by step.
				 *
				 * loud: 0x2; whisper: 0x7
				 */
				regmap_update_bits(rk3308->regmap,
						   RK3308_DAC_DIG_CON04,
						   RK3308_DAC_CIC_IF_GAIN_MSK,
						   dgain);
				usleep_range(200, 300);  /* estimated value */
			}

#if !DEBUG_POP_ALWAYS
			rk3308_headphone_ctl(rk3308, 0);
			rk3308_speaker_ctl(rk3308, 0);
#endif
		} else {
#if !DEBUG_POP_ALWAYS
			if (rk3308->dac_output == DAC_LINEOUT)
				rk3308_speaker_ctl(rk3308, 1);
			else if (rk3308->dac_output == DAC_HPOUT)
				rk3308_headphone_ctl(rk3308, 1);

			if (rk3308->delay_start_play_ms)
				msleep(rk3308->delay_start_play_ms);
#endif
			for (dgain = 0x7; dgain >= 0x2; dgain--) {
				/*
				 * Keep the min -> max digital CIC interpolation
				 * filter gain step by step
				 *
				 * loud: 0x2; whisper: 0x7
				 */
				regmap_update_bits(rk3308->regmap,
						   RK3308_DAC_DIG_CON04,
						   RK3308_DAC_CIC_IF_GAIN_MSK,
						   dgain);
				usleep_range(200, 300);  /* estimated value */
			}
		}
	}

	return 0;
}

static int rk3308_codec_digital_fadein(struct rk3308_codec_priv *rk3308)
{
	unsigned int dgain, dgain_ref;

	if (rk3308->hpout_l_dgain != rk3308->hpout_r_dgain) {
		pr_warn("HPOUT l_dgain: 0x%x != r_dgain: 0x%x\n",
			rk3308->hpout_l_dgain, rk3308->hpout_r_dgain);
		dgain_ref = min(rk3308->hpout_l_dgain, rk3308->hpout_r_dgain);
	} else {
		dgain_ref = rk3308->hpout_l_dgain;
	}

	/*
	 * We'd better change the gain of the left and right channels
	 * at the same time to avoid different listening
	 */
	for (dgain = RK3308_DAC_L_HPOUT_GAIN_NDB_39;
	     dgain <= dgain_ref; dgain++) {
		/* Step 02 decrease dgains for de-pop */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON05,
				   RK3308_DAC_L_HPOUT_GAIN_MSK,
				   dgain);

		/* Step 02 decrease dgains for de-pop */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON06,
				   RK3308_DAC_R_HPOUT_GAIN_MSK,
				   dgain);
	}

	return 0;
}

static int rk3308_codec_digital_fadeout(struct rk3308_codec_priv *rk3308)
{
	unsigned int l_dgain, r_dgain;

	/*
	 * Note. In the step2, adjusting the register step by step to
	 * the appropriate value and taking 20ms as time step
	 */
	regmap_read(rk3308->regmap, RK3308_DAC_ANA_CON05, &l_dgain);
	l_dgain &= RK3308_DAC_L_HPOUT_GAIN_MSK;

	regmap_read(rk3308->regmap, RK3308_DAC_ANA_CON06, &r_dgain);
	r_dgain &= RK3308_DAC_R_HPOUT_GAIN_MSK;

	if (l_dgain != r_dgain) {
		pr_warn("HPOUT l_dgain: 0x%x != r_dgain: 0x%x\n",
			l_dgain, r_dgain);
		l_dgain = min(l_dgain, r_dgain);
	}

	/*
	 * We'd better change the gain of the left and right channels
	 * at the same time to avoid different listening
	 */
	while (l_dgain >= RK3308_DAC_L_HPOUT_GAIN_NDB_39) {
		/* Step 02 decrease dgains for de-pop */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON05,
				   RK3308_DAC_L_HPOUT_GAIN_MSK,
				   l_dgain);

		/* Step 02 decrease dgains for de-pop */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON06,
				   RK3308_DAC_R_HPOUT_GAIN_MSK,
				   l_dgain);

		usleep_range(200, 300);  /* estimated value */

		if (l_dgain == RK3308_DAC_L_HPOUT_GAIN_NDB_39)
			break;

		l_dgain--;
	}

	return 0;
}

static int rk3308_codec_dac_lineout_enable(struct rk3308_codec_priv *rk3308)
{
	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/* Step 04 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON15,
				   RK3308_DAC_LINEOUT_POP_SOUND_L_MSK |
				   RK3308_DAC_LINEOUT_POP_SOUND_R_MSK,
				   RK3308_DAC_L_SEL_DC_FROM_INTERNAL |
				   RK3308_DAC_R_SEL_DC_FROM_INTERNAL);
	}

	/* Step 07 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_EN |
			   RK3308_DAC_R_LINEOUT_EN,
			   RK3308_DAC_L_LINEOUT_EN |
			   RK3308_DAC_R_LINEOUT_EN);

	udelay(20);

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/* Step 10 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON15,
				   RK3308_DAC_LINEOUT_POP_SOUND_L_MSK |
				   RK3308_DAC_LINEOUT_POP_SOUND_R_MSK,
				   RK3308_DAC_L_SEL_LINEOUT_FROM_INTERNAL |
				   RK3308_DAC_R_SEL_LINEOUT_FROM_INTERNAL);

		udelay(20);
	}

	/* Step 19 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_UNMUTE |
			   RK3308_DAC_R_LINEOUT_UNMUTE,
			   RK3308_DAC_L_LINEOUT_UNMUTE |
			   RK3308_DAC_R_LINEOUT_UNMUTE);
	udelay(20);

	return 0;
}

static int rk3308_codec_dac_lineout_disable(struct rk3308_codec_priv *rk3308)
{
	/* Step 08 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_UNMUTE |
			   RK3308_DAC_R_LINEOUT_UNMUTE,
			   RK3308_DAC_L_LINEOUT_MUTE |
			   RK3308_DAC_R_LINEOUT_MUTE);

	/* Step 09 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_EN |
			   RK3308_DAC_R_LINEOUT_EN,
			   RK3308_DAC_L_LINEOUT_DIS |
			   RK3308_DAC_R_LINEOUT_DIS);

	return 0;
}

static int rk3308_codec_dac_hpout_enable(struct rk3308_codec_priv *rk3308)
{
	/* Step 03 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_HPOUT_POP_SOUND_L_MSK |
			   RK3308_DAC_HPOUT_POP_SOUND_R_MSK,
			   RK3308_DAC_HPOUT_POP_SOUND_L_WORK |
			   RK3308_DAC_HPOUT_POP_SOUND_R_WORK);

	udelay(20);

	/* Step 07 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_EN |
			   RK3308_DAC_R_HPOUT_EN,
			   RK3308_DAC_L_HPOUT_EN |
			   RK3308_DAC_R_HPOUT_EN);

	udelay(20);

	/* Step 08 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_WORK |
			   RK3308_DAC_R_HPOUT_WORK,
			   RK3308_DAC_L_HPOUT_WORK |
			   RK3308_DAC_R_HPOUT_WORK);

	udelay(20);

	/* Step 16 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_UNMUTE |
			   RK3308_DAC_R_HPOUT_UNMUTE,
			   RK3308_DAC_L_HPOUT_UNMUTE |
			   RK3308_DAC_R_HPOUT_UNMUTE);

	udelay(20);

	return 0;
}

static int rk3308_codec_dac_hpout_disable(struct rk3308_codec_priv *rk3308)
{
	/* Step 03 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_HPOUT_POP_SOUND_L_MSK |
			   RK3308_DAC_HPOUT_POP_SOUND_R_MSK,
			   RK3308_DAC_HPOUT_POP_SOUND_L_INIT |
			   RK3308_DAC_HPOUT_POP_SOUND_R_INIT);

	/* Step 07 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_EN |
			   RK3308_DAC_R_HPOUT_EN,
			   RK3308_DAC_L_HPOUT_DIS |
			   RK3308_DAC_R_HPOUT_DIS);

	/* Step 08 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_WORK |
			   RK3308_DAC_R_HPOUT_WORK,
			   RK3308_DAC_L_HPOUT_INIT |
			   RK3308_DAC_R_HPOUT_INIT);

	/* Step 16 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_UNMUTE |
			   RK3308_DAC_R_HPOUT_UNMUTE,
			   RK3308_DAC_L_HPOUT_MUTE |
			   RK3308_DAC_R_HPOUT_MUTE);

	return 0;
}

static int rk3308_codec_dac_switch(struct rk3308_codec_priv *rk3308,
				   int dac_output)
{	int ret = 0;

	if (rk3308->dac_output == dac_output) {
		dev_info(rk3308->plat_dev,
			 "Don't need to change dac_output: %d\n", dac_output);
		goto out;
	}

	switch (dac_output) {
	case DAC_LINEOUT:
	case DAC_HPOUT:
	case DAC_LINEOUT_HPOUT:
		break;
	default:
		dev_err(rk3308->plat_dev, "Unknown value: %d\n", dac_output);
		ret = -EINVAL;
		goto out;
	}

	if (rk3308_codec_get_dac_path_state(rk3308) == PATH_BUSY) {
		/*
		 * We can only switch the audio path to LINEOUT or HPOUT on
		 * codec during playbacking, otherwise, just update the
		 * dac_output flag.
		 */
		switch (dac_output) {
		case DAC_LINEOUT:
			rk3308_headphone_ctl(rk3308, 0);
			rk3308_speaker_ctl(rk3308, 1);
			rk3308_codec_dac_hpout_disable(rk3308);
			rk3308_codec_dac_lineout_enable(rk3308);
			break;
		case DAC_HPOUT:
			rk3308_speaker_ctl(rk3308, 0);
			rk3308_headphone_ctl(rk3308, 1);
			rk3308_codec_dac_lineout_disable(rk3308);
			rk3308_codec_dac_hpout_enable(rk3308);
			break;
		case DAC_LINEOUT_HPOUT:
			rk3308_speaker_ctl(rk3308, 1);
			rk3308_headphone_ctl(rk3308, 1);
			rk3308_codec_dac_lineout_enable(rk3308);
			rk3308_codec_dac_hpout_enable(rk3308);
			break;
		default:
			break;
		}
	}

	rk3308->dac_output = dac_output;
out:
	dev_dbg(rk3308->plat_dev, "switch dac_output to: %d\n",
		rk3308->dac_output);

	return ret;
}

static int rk3308_codec_dac_enable(struct rk3308_codec_priv *rk3308)
{
	/*
	 * Note1. If the ACODEC_DAC_ANA_CON12[6] or ACODEC_DAC_ANA_CON12[2]
	 * is set to 0x1, ignoring the step9~12.
	 */

	/*
	 * Note2. If the ACODEC_ DAC_ANA_CON12[7] or ACODEC_DAC_ANA_CON12[3]
	 * is set to 0x1, the ADC0 or ADC1 should be enabled firstly, and
	 * please refer to Enable ADC Configuration Standard Usage Flow(expect
	 * step7~step9,step14).
	 */

	/*
	 * Note3. If no opening the line out, ignoring the step6, step17 and
	 * step19.
	 */

	/*
	 * Note4. If no opening the headphone out, ignoring the step3,step7~8,
	 * step16 and step18.
	 */

	/*
	 * Note5. In the step18, adjust the register step by step to the
	 * appropriate value and taking 10ms as one time step
	 */

	/*
	 * 1. Set the ACODEC_DAC_ANA_CON0[0] to 0x1, to enable the current
	 * source of DAC
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_CURRENT_MSK,
			   RK3308_DAC_CURRENT_EN);

	udelay(20);

	/*
	 * 2. Set the ACODEC_DAC_ANA_CON1[6] and ACODEC_DAC_ANA_CON1[2] to 0x1,
	 * to enable the reference voltage buffer
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_BUF_REF_L_MSK |
			   RK3308_DAC_BUF_REF_R_MSK,
			   RK3308_DAC_BUF_REF_L_EN |
			   RK3308_DAC_BUF_REF_R_EN);

	/* Waiting the stable reference voltage */
	mdelay(1);

	if (rk3308->dac_output == DAC_HPOUT ||
	    rk3308->dac_output == DAC_LINEOUT_HPOUT) {
		/* Step 03 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
				   RK3308_DAC_HPOUT_POP_SOUND_L_MSK |
				   RK3308_DAC_HPOUT_POP_SOUND_R_MSK,
				   RK3308_DAC_HPOUT_POP_SOUND_L_WORK |
				   RK3308_DAC_HPOUT_POP_SOUND_R_WORK);

		udelay(20);
	}

	if (rk3308->codec_ver == ACODEC_VERSION_B &&
	    (rk3308->dac_output == DAC_LINEOUT ||
	     rk3308->dac_output == DAC_LINEOUT_HPOUT)) {
		/* Step 04 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON15,
				   RK3308_DAC_LINEOUT_POP_SOUND_L_MSK |
				   RK3308_DAC_LINEOUT_POP_SOUND_R_MSK,
				   RK3308_DAC_L_SEL_DC_FROM_INTERNAL |
				   RK3308_DAC_R_SEL_DC_FROM_INTERNAL);

		udelay(20);
	}

	/* Step 05 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_EN |
			   RK3308_DAC_R_HPMIX_EN,
			   RK3308_DAC_L_HPMIX_EN |
			   RK3308_DAC_R_HPMIX_EN);

	/* Waiting the stable HPMIX */
	mdelay(1);

	/* Step 06. Reset HPMIX and recover HPMIX gains */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_WORK |
			   RK3308_DAC_R_HPMIX_WORK,
			   RK3308_DAC_L_HPMIX_INIT |
			   RK3308_DAC_R_HPMIX_INIT);
	udelay(50);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_WORK |
			   RK3308_DAC_R_HPMIX_WORK,
			   RK3308_DAC_L_HPMIX_WORK |
			   RK3308_DAC_R_HPMIX_WORK);

	udelay(20);

	if (rk3308->dac_output == DAC_LINEOUT ||
	    rk3308->dac_output == DAC_LINEOUT_HPOUT) {
		/* Step 07 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
				   RK3308_DAC_L_LINEOUT_EN |
				   RK3308_DAC_R_LINEOUT_EN,
				   RK3308_DAC_L_LINEOUT_EN |
				   RK3308_DAC_R_LINEOUT_EN);

		udelay(20);
	}

	if (rk3308->dac_output == DAC_HPOUT ||
	    rk3308->dac_output == DAC_LINEOUT_HPOUT) {
		/* Step 08 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
				   RK3308_DAC_L_HPOUT_EN |
				   RK3308_DAC_R_HPOUT_EN,
				   RK3308_DAC_L_HPOUT_EN |
				   RK3308_DAC_R_HPOUT_EN);

		udelay(20);

		/* Step 09 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
				   RK3308_DAC_L_HPOUT_WORK |
				   RK3308_DAC_R_HPOUT_WORK,
				   RK3308_DAC_L_HPOUT_WORK |
				   RK3308_DAC_R_HPOUT_WORK);

		udelay(20);
	}

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/* Step 10 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON15,
				   RK3308_DAC_LINEOUT_POP_SOUND_L_MSK |
				   RK3308_DAC_LINEOUT_POP_SOUND_R_MSK,
				   RK3308_DAC_L_SEL_LINEOUT_FROM_INTERNAL |
				   RK3308_DAC_R_SEL_LINEOUT_FROM_INTERNAL);

		udelay(20);
	}

	/* Step 11 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_REF_EN |
			   RK3308_DAC_R_REF_EN,
			   RK3308_DAC_L_REF_EN |
			   RK3308_DAC_R_REF_EN);

	udelay(20);

	/* Step 12 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_CLK_EN |
			   RK3308_DAC_R_CLK_EN,
			   RK3308_DAC_L_CLK_EN |
			   RK3308_DAC_R_CLK_EN);

	udelay(20);

	/* Step 13 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_EN |
			   RK3308_DAC_R_DAC_EN,
			   RK3308_DAC_L_DAC_EN |
			   RK3308_DAC_R_DAC_EN);

	udelay(20);

	/* Step 14 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_WORK |
			   RK3308_DAC_R_DAC_WORK,
			   RK3308_DAC_L_DAC_WORK |
			   RK3308_DAC_R_DAC_WORK);

	udelay(20);

	/* Step 15 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_SEL_MSK |
			   RK3308_DAC_R_HPMIX_SEL_MSK,
			   RK3308_DAC_L_HPMIX_I2S |
			   RK3308_DAC_R_HPMIX_I2S);

	udelay(20);

	/* Step 16 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_UNMUTE |
			   RK3308_DAC_R_HPMIX_UNMUTE,
			   RK3308_DAC_L_HPMIX_UNMUTE |
			   RK3308_DAC_R_HPMIX_UNMUTE);

	udelay(20);

	/* Step 17: Put configuration HPMIX Gain to DAPM */

	if (rk3308->dac_output == DAC_HPOUT ||
	    rk3308->dac_output == DAC_LINEOUT_HPOUT) {
		/* Step 18 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
				   RK3308_DAC_L_HPOUT_UNMUTE |
				   RK3308_DAC_R_HPOUT_UNMUTE,
				   RK3308_DAC_L_HPOUT_UNMUTE |
				   RK3308_DAC_R_HPOUT_UNMUTE);

		udelay(20);
	}

	if (rk3308->dac_output == DAC_LINEOUT ||
	    rk3308->dac_output == DAC_LINEOUT_HPOUT) {
		/* Step 19 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
				   RK3308_DAC_L_LINEOUT_UNMUTE |
				   RK3308_DAC_R_LINEOUT_UNMUTE,
				   RK3308_DAC_L_LINEOUT_UNMUTE |
				   RK3308_DAC_R_LINEOUT_UNMUTE);
		udelay(20);
	}

	/* Step 20, put configuration HPOUT gain to DAPM control */
	/* Step 21, put configuration LINEOUT gain to DAPM control */

	if (rk3308->dac_output == DAC_HPOUT ||
	    rk3308->dac_output == DAC_LINEOUT_HPOUT) {
		/* Just for HPOUT */
		rk3308_codec_digital_fadein(rk3308);
	}

	rk3308->dac_endisable = true;

	/* TODO: TRY TO TEST DRIVE STRENGTH */

	return 0;
}

static int rk3308_codec_dac_disable(struct rk3308_codec_priv *rk3308)
{
	/*
	 * Step 00 skipped. Keep the DAC channel work and input the mute signal.
	 */

	/* Step 01 skipped. May set the min gain for LINEOUT. */

	/* Step 02 skipped. May set the min gain for HPOUT. */

	if (rk3308->dac_output == DAC_HPOUT ||
	    rk3308->dac_output == DAC_LINEOUT_HPOUT) {
		/* Just for HPOUT */
		rk3308_codec_digital_fadeout(rk3308);
	}

	/* Step 03 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_UNMUTE |
			   RK3308_DAC_R_HPMIX_UNMUTE,
			   RK3308_DAC_L_HPMIX_UNMUTE |
			   RK3308_DAC_R_HPMIX_UNMUTE);

	/* Step 04 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_SEL_MSK |
			   RK3308_DAC_R_HPMIX_SEL_MSK,
			   RK3308_DAC_L_HPMIX_NONE |
			   RK3308_DAC_R_HPMIX_NONE);
	/* Step 05 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_UNMUTE |
			   RK3308_DAC_R_HPOUT_UNMUTE,
			   RK3308_DAC_L_HPOUT_MUTE |
			   RK3308_DAC_R_HPOUT_MUTE);

	/* Step 06 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_WORK |
			   RK3308_DAC_R_DAC_WORK,
			   RK3308_DAC_L_DAC_INIT |
			   RK3308_DAC_R_DAC_INIT);

	/* Step 07 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_EN |
			   RK3308_DAC_R_HPOUT_EN,
			   RK3308_DAC_L_HPOUT_DIS |
			   RK3308_DAC_R_HPOUT_DIS);

	/* Step 08 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_UNMUTE |
			   RK3308_DAC_R_LINEOUT_UNMUTE,
			   RK3308_DAC_L_LINEOUT_MUTE |
			   RK3308_DAC_R_LINEOUT_MUTE);

	/* Step 09 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_EN |
			   RK3308_DAC_R_LINEOUT_EN,
			   RK3308_DAC_L_LINEOUT_DIS |
			   RK3308_DAC_R_LINEOUT_DIS);

	/* Step 10 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_EN |
			   RK3308_DAC_R_HPMIX_EN,
			   RK3308_DAC_L_HPMIX_DIS |
			   RK3308_DAC_R_HPMIX_DIS);

	/* Step 11 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_EN |
			   RK3308_DAC_R_DAC_EN,
			   RK3308_DAC_L_DAC_DIS |
			   RK3308_DAC_R_DAC_DIS);

	/* Step 12 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_CLK_EN |
			   RK3308_DAC_R_CLK_EN,
			   RK3308_DAC_L_CLK_DIS |
			   RK3308_DAC_R_CLK_DIS);

	/* Step 13 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_REF_EN |
			   RK3308_DAC_R_REF_EN,
			   RK3308_DAC_L_REF_DIS |
			   RK3308_DAC_R_REF_DIS);

	/* Step 14 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_HPOUT_POP_SOUND_L_MSK |
			   RK3308_DAC_HPOUT_POP_SOUND_R_MSK,
			   RK3308_DAC_HPOUT_POP_SOUND_L_INIT |
			   RK3308_DAC_HPOUT_POP_SOUND_R_INIT);

	/* Step 15 */
	if (rk3308->codec_ver == ACODEC_VERSION_B &&
	    (rk3308->dac_output == DAC_LINEOUT ||
	     rk3308->dac_output == DAC_LINEOUT_HPOUT)) {
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON15,
				   RK3308_DAC_LINEOUT_POP_SOUND_L_MSK |
				   RK3308_DAC_LINEOUT_POP_SOUND_R_MSK,
				   RK3308_DAC_L_SEL_DC_FROM_VCM |
				   RK3308_DAC_R_SEL_DC_FROM_VCM);
	}

	/* Step 16 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_BUF_REF_L_EN |
			   RK3308_DAC_BUF_REF_R_EN,
			   RK3308_DAC_BUF_REF_L_DIS |
			   RK3308_DAC_BUF_REF_R_DIS);

	/* Step 17 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_CURRENT_EN,
			   RK3308_DAC_CURRENT_DIS);

	/* Step 18 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_WORK |
			   RK3308_DAC_R_HPOUT_WORK,
			   RK3308_DAC_L_HPOUT_INIT |
			   RK3308_DAC_R_HPOUT_INIT);

	/* Step 19 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_WORK |
			   RK3308_DAC_R_HPMIX_WORK,
			   RK3308_DAC_L_HPMIX_WORK |
			   RK3308_DAC_R_HPMIX_WORK);

	/* Step 20 skipped, may set the min gain for HPOUT. */

	/*
	 * Note2. If the ACODEC_DAC_ANA_CON12[7] or ACODEC_DAC_ANA_CON12[3]
	 * is set to 0x1, add the steps from the section Disable ADC
	 * Configuration Standard Usage Flow after complete the step 19
	 *
	 * IF USING LINE-IN
	 * rk3308_codec_adc_ana_disable(rk3308, type);
	 */

	rk3308->dac_endisable = false;

	return 0;
}

static int rk3308_codec_power_on(struct rk3308_codec_priv *rk3308)
{
	unsigned int v;

	/* 0. Supply the power of digital part and reset the Audio Codec */
	/* Do nothing */

	/*
	 * 1. Configure ACODEC_DAC_ANA_CON1[1:0] and ACODEC_DAC_ANA_CON1[5:4]
	 *    to 0x1, to setup dc voltage of the DAC channel output.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_HPOUT_POP_SOUND_L_MSK,
			   RK3308_DAC_HPOUT_POP_SOUND_L_INIT);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_HPOUT_POP_SOUND_R_MSK,
			   RK3308_DAC_HPOUT_POP_SOUND_R_INIT);

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/*
		 * 2. Configure ACODEC_DAC_ANA_CON15[1:0] and
		 *    ACODEC_DAC_ANA_CON15[5:4] to 0x1, to setup dc voltage of
		 *    the DAC channel output.
		 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON15,
				   RK3308_DAC_LINEOUT_POP_SOUND_L_MSK,
				   RK3308_DAC_L_SEL_DC_FROM_VCM);
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON15,
				   RK3308_DAC_LINEOUT_POP_SOUND_R_MSK,
				   RK3308_DAC_R_SEL_DC_FROM_VCM);
	}

	/*
	 * 3. Configure the register ACODEC_ADC_ANA_CON10[3:0] to 7’b000_0001.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK,
			   RK3308_ADC_SEL_I(0x1));

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/*
		 * 4. Configure the register ACODEC_ADC_ANA_CON14[3:0] to
		 *    4’b0001.
		 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON14,
				   RK3308_DAC_CURRENT_CHARGE_MSK,
				   RK3308_DAC_SEL_I(0x1));
	}

	/* 5. Supply the power of the analog part(AVDD,AVDDRV) */

	/*
	 * 6. Configure the register ACODEC_ADC_ANA_CON10[7] to 0x1 to setup
	 *    reference voltage
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_REF_EN, RK3308_ADC_REF_EN);

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/*
		 * 7. Configure the register ACODEC_ADC_ANA_CON14[4] to 0x1 to
		 *    setup reference voltage
		 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON14,
				   RK3308_DAC_VCM_LINEOUT_EN,
				   RK3308_DAC_VCM_LINEOUT_EN);
	}

	/*
	 * 8. Change the register ACODEC_ADC_ANA_CON10[6:0] from the 0x1 to
	 *    0x7f step by step or configure the ACODEC_ADC_ANA_CON10[6:0] to
	 *    0x7f directly. Here the slot time of the step is 200us.
	 */
	for (v = 0x1; v <= 0x7f; v++) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
				   RK3308_ADC_CURRENT_CHARGE_MSK,
				   v);
		udelay(200);
	}

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/*
		 * 9. Change the register ACODEC_ADC_ANA_CON14[3:0] from the 0x1
		 *    to 0xf step by step or configure the
		 *    ACODEC_ADC_ANA_CON14[3:0] to 0xf directly. Here the slot
		 *    time of the step is 200us.
		 */
		for (v = 0x1; v <= 0xf; v++) {
			regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON14,
					   RK3308_DAC_CURRENT_CHARGE_MSK,
					   v);
			udelay(200);
		}
	}

	/* 10. Wait until the voltage of VCM keeps stable at the AVDD/2 */
	msleep(20);	/* estimated value */

	/*
	 * 11. Configure the register ACODEC_ADC_ANA_CON10[6:0] to the
	 *     appropriate value(expect 0x0) for reducing power.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK, 0x7c);

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/*
		 * 12. Configure the register ACODEC_DAC_ANA_CON14[6:0] to the
		 *     appropriate value(expect 0x0) for reducing power.
		 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON14,
				   RK3308_DAC_CURRENT_CHARGE_MSK, 0xf);
	}

	return 0;
}

static int rk3308_codec_power_off(struct rk3308_codec_priv *rk3308)
{
	unsigned int v;

	/*
	 * 0. Keep the power on and disable the DAC and ADC path according to
	 *    the section power on configuration standard usage flow.
	 */

	/*
	 * 1. Configure the register ACODEC_ADC_ANA_CON10[6:0] to 7’b000_0001.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK,
			   RK3308_ADC_SEL_I(0x1));

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/*
		 * 2. Configure the register ACODEC_DAC_ANA_CON14[3:0] to
		 *    4’b0001.
		 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON14,
				   RK3308_DAC_CURRENT_CHARGE_MSK,
				   RK3308_DAC_SEL_I(0x1));
	}

	/* 3. Configure the register ACODEC_ADC_ANA_CON10[7] to 0x0 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_REF_EN,
			   RK3308_ADC_REF_DIS);

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/* 4. Configure the register ACODEC_DAC_ANA_CON14[7] to 0x0 */
		regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON14,
				   RK3308_DAC_VCM_LINEOUT_EN,
				   RK3308_DAC_VCM_LINEOUT_DIS);
	}

	/*
	 * 5. Change the register ACODEC_ADC_ANA_CON10[6:0] from the 0x1 to 0x7f
	 *    step by step or configure the ACODEC_ADC_ANA_CON10[6:0] to 0x7f
	 *    directly. Here the slot time of the step is 200us.
	 */
	for (v = 0x1; v <= 0x7f; v++) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
				   RK3308_ADC_CURRENT_CHARGE_MSK,
				   v);
		udelay(200);
	}

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/*
		 * 6. Change the register ACODEC_DAC_ANA_CON14[3:0] from the 0x1
		 *    to 0xf step by step or configure the
		 *    ACODEC_DAC_ANA_CON14[3:0] to 0xf directly. Here the slot
		 *    time of the step is 200us.
		 */
		for (v = 0x1; v <= 0x7f; v++) {
			regmap_update_bits(rk3308->regmap,
					   RK3308_ADC_ANA_CON10(0),
					   RK3308_ADC_CURRENT_CHARGE_MSK,
					   v);
			udelay(200);
		}
	}

	/* 7. Wait until the voltage of VCM keeps stable at the AGND */
	msleep(20);	/* estimated value */

	/* 8. Power off the analog power supply */
	/* 9. Power off the digital power supply */

	/* Do something via hardware */

	return 0;
}

static int rk3308_codec_headset_detect_enable(struct rk3308_codec_priv *rk3308)
{
	/*
	 * Set ACODEC_DAC_ANA_CON0[1] to 0x1, to enable the headset insert
	 * detection
	 *
	 * Note. When the voltage of PAD HPDET> 8*AVDD/9, the output value of
	 * the pin_hpdet will be set to 0x1 and assert a interrupt
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_HEADPHONE_DET_MSK,
			   RK3308_DAC_HEADPHONE_DET_EN);

	return 0;
}

static int rk3308_codec_headset_detect_disable(struct rk3308_codec_priv *rk3308)
{
	/*
	 * Set ACODEC_DAC_ANA_CON0[1] to 0x0, to disable the headset insert
	 * detection
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_HEADPHONE_DET_MSK,
			   RK3308_DAC_HEADPHONE_DET_DIS);

	return 0;
}

static int rk3308_codec_check_i2s_sdis(struct rk3308_codec_priv *rk3308,
				       int num)
{
	int i, j, ret = 0;

	switch (num) {
	case 1:
		rk3308->which_i2s = ACODEC_TO_I2S1_2CH;
		break;
	case 2:
		rk3308->which_i2s = ACODEC_TO_I2S3_4CH;
		break;
	case 4:
		rk3308->which_i2s = ACODEC_TO_I2S2_8CH;
		break;
	default:
		dev_err(rk3308->plat_dev, "Invalid i2s sdis num: %d\n", num);
		ret = -EINVAL;
		goto err;
	}

	for (i = 0; i < num; i++) {
		if (rk3308->i2s_sdis[i] > ADC_LR_GROUP_MAX - 1) {
			dev_err(rk3308->plat_dev,
				"i2s_sdis[%d]: %d is overflow\n",
				i, rk3308->i2s_sdis[i]);
			ret = -EINVAL;
			goto err;
		}

		for (j = 0; j < num; j++) {
			if (i == j)
				continue;

			if (rk3308->i2s_sdis[i] == rk3308->i2s_sdis[j]) {
				dev_err(rk3308->plat_dev,
					"Invalid i2s_sdis: [%d]%d == [%d]%d\n",
					i, rk3308->i2s_sdis[i],
					j, rk3308->i2s_sdis[j]);
				ret = -EINVAL;
				goto err;
			}
		}
	}

err:
	return ret;
}

static int rk3308_codec_adc_grps_route_config(struct rk3308_codec_priv *rk3308)
{
	int idx = 0;

	if (rk3308->which_i2s == ACODEC_TO_I2S2_8CH) {
		for (idx = 0; idx < rk3308->to_i2s_grps; idx++) {
			regmap_write(rk3308->grf, GRF_SOC_CON1,
				     GRF_I2S2_8CH_SDI(idx, rk3308->i2s_sdis[idx]));
		}
	} else if (rk3308->which_i2s == ACODEC_TO_I2S3_4CH) {
		for (idx = 0; idx < rk3308->to_i2s_grps; idx++) {
			regmap_write(rk3308->grf, GRF_SOC_CON1,
				     GRF_I2S3_4CH_SDI(idx, rk3308->i2s_sdis[idx]));
		}
	} else if (rk3308->which_i2s == ACODEC_TO_I2S1_2CH) {
		regmap_write(rk3308->grf, GRF_SOC_CON1,
			     GRF_I2S1_2CH_SDI(rk3308->i2s_sdis[idx]));
	}

	return 0;
}

/* Put default one-to-one mapping */
static int rk3308_codec_adc_grps_route_default(struct rk3308_codec_priv *rk3308)
{
	unsigned int idx;

	/*
	 * The GRF values may be kept the previous status after hot reboot,
	 * if the property 'rockchip,adc-grps-route' is not set, we need to
	 * recover default the order of sdi/sdo for i2s2_8ch/i2s3_8ch/i2s1_2ch.
	 */
	regmap_write(rk3308->grf, GRF_SOC_CON1,
		     GRF_I2S1_2CH_SDI(0));

	for (idx = 0; idx < 2; idx++) {
		regmap_write(rk3308->grf, GRF_SOC_CON1,
			     GRF_I2S3_4CH_SDI(idx, idx));
	}

	/* Using i2s2_8ch by default. */
	rk3308->which_i2s = ACODEC_TO_I2S2_8CH;
	rk3308->to_i2s_grps = ADC_LR_GROUP_MAX;

	for (idx = 0; idx < ADC_LR_GROUP_MAX; idx++) {
		rk3308->i2s_sdis[idx] = idx;
		regmap_write(rk3308->grf, GRF_SOC_CON1,
			     GRF_I2S2_8CH_SDI(idx, idx));
	}

	return 0;
}

static int rk3308_codec_adc_grps_route(struct rk3308_codec_priv *rk3308,
				       struct device_node *np)
{
	int num, ret;

	num = of_count_phandle_with_args(np, "rockchip,adc-grps-route", NULL);
	if (num < 0) {
		if (num == -ENOENT) {
			/* Not use 'rockchip,adc-grps-route' property here */
			rk3308_codec_adc_grps_route_default(rk3308);
			ret = 0;
		} else {
			dev_err(rk3308->plat_dev,
				"Failed to read 'rockchip,adc-grps-route' num: %d\n",
				num);
			ret = num;
		}
		return ret;
	}

	ret = of_property_read_u32_array(np, "rockchip,adc-grps-route",
					 rk3308->i2s_sdis, num);
	if (ret < 0) {
		dev_err(rk3308->plat_dev,
			"Failed to read 'rockchip,adc-grps-route': %d\n",
			ret);
		return ret;
	}

	ret = rk3308_codec_check_i2s_sdis(rk3308, num);
	if (ret < 0) {
		dev_err(rk3308->plat_dev,
			"Failed to check i2s_sdis: %d\n", ret);
		return ret;
	}

	rk3308->to_i2s_grps = num;

	rk3308_codec_adc_grps_route_config(rk3308);

	return 0;
}

static int check_micbias(int micbias)
{
	switch (micbias) {
	case RK3308_ADC_MICBIAS_VOLT_0_85:
	case RK3308_ADC_MICBIAS_VOLT_0_8:
	case RK3308_ADC_MICBIAS_VOLT_0_75:
	case RK3308_ADC_MICBIAS_VOLT_0_7:
	case RK3308_ADC_MICBIAS_VOLT_0_65:
	case RK3308_ADC_MICBIAS_VOLT_0_6:
	case RK3308_ADC_MICBIAS_VOLT_0_55:
	case RK3308_ADC_MICBIAS_VOLT_0_5:
		return 0;
	}

	return -EINVAL;
}

static bool handle_loopback(struct rk3308_codec_priv *rk3308)
{
	/* The version B doesn't need to handle loopback. */
	if (rk3308->codec_ver == ACODEC_VERSION_B)
		return false;

	switch (rk3308->loopback_grp) {
	case 0:
	case 1:
	case 2:
	case 3:
		return true;
	}

	return false;
}

static bool has_en_always_grps(struct rk3308_codec_priv *rk3308)
{
	int idx;

	if (rk3308->en_always_grps_num) {
		for (idx = 0; idx < ADC_LR_GROUP_MAX; idx++) {
			if (rk3308->en_always_grps[idx] >= 0 &&
			    rk3308->en_always_grps[idx] <= ADC_LR_GROUP_MAX - 1)
				return true;
		}
	}

	return false;
}

static int rk3308_codec_micbias_enable(struct rk3308_codec_priv *rk3308,
				       int micbias)
{
	int ret;

	if (rk3308->ext_micbias != EXT_MICBIAS_NONE)
		return 0;

	/* 0. Power up the ACODEC and keep the AVDDH stable */

	/* Step 1. Configure ACODEC_ADC_ANA_CON7[2:0] to the certain value */
	ret = check_micbias(micbias);
	if (ret < 0) {
		dev_err(rk3308->plat_dev, "This is an invalid micbias: %d\n",
			micbias);
		return ret;
	}

	/*
	 * Note: Only the reg (ADC_ANA_CON7+0x0)[2:0] represent the level range
	 * control signal of MICBIAS voltage
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(0),
			   RK3308_ADC_LEVEL_RANGE_MICBIAS_MSK,
			   micbias);

	/* Step 2. Wait until the VCMH keep stable */
	msleep(20);	/* estimated value */

	/*
	 * Step 3. Configure ACODEC_ADC_ANA_CON8[4] to 0x1
	 *
	 * Note: Only the reg (ADC_ANA_CON8+0x0)[4] represent the enable
	 * signal of current source for MICBIAS
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON08(0),
			   RK3308_ADC_MICBIAS_CURRENT_MSK,
			   RK3308_ADC_MICBIAS_CURRENT_EN);

	/*
	 * Step 4. Configure the (ADC_ANA_CON7+0x40)[3] or
	 * (ADC_ANA_CON7+0x80)[3] to 0x1.
	 *
	 * (ADC_ANA_CON7+0x40)[3] used to control the MICBIAS1, and
	 * (ADC_ANA_CON7+0x80)[3] used to control the MICBIAS2
	 */
	if (rk3308->micbias1)
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(1),
				   RK3308_ADC_MIC_BIAS_BUF_EN,
				   RK3308_ADC_MIC_BIAS_BUF_EN);

	if (rk3308->micbias2)
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(2),
				   RK3308_ADC_MIC_BIAS_BUF_EN,
				   RK3308_ADC_MIC_BIAS_BUF_EN);

	/* waiting micbias stabled*/
	mdelay(50);

	rk3308->enable_micbias = true;

	return 0;
}

static int rk3308_codec_micbias_disable(struct rk3308_codec_priv *rk3308)
{
	if (rk3308->ext_micbias != EXT_MICBIAS_NONE)
		return 0;

	/* Step 0. Enable the MICBIAS and keep the Audio Codec stable */
	/* Do nothing */

	/*
	 * Step 1. Configure the (ADC_ANA_CON7+0x40)[3] or
	 * (ADC_ANA_CON7+0x80)[3] to 0x0
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(1),
			   RK3308_ADC_MIC_BIAS_BUF_EN,
			   RK3308_ADC_MIC_BIAS_BUF_DIS);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(2),
			   RK3308_ADC_MIC_BIAS_BUF_EN,
			   RK3308_ADC_MIC_BIAS_BUF_DIS);

	/*
	 * Step 2. Configure ACODEC_ADC_ANA_CON8[4] to 0x0
	 *
	 * Note: Only the reg (ADC_ANA_CON8+0x0)[4] represent the enable
	 * signal of current source for MICBIAS
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON08(0),
			   RK3308_ADC_MICBIAS_CURRENT_MSK,
			   RK3308_ADC_MICBIAS_CURRENT_DIS);

	rk3308->enable_micbias = false;

	return 0;
}

static int rk3308_codec_adc_reinit_mics(struct rk3308_codec_priv *rk3308,
					int type)
{
	int idx, grp;

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 1 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_ADC_WORK |
				   RK3308_ADC_CH2_ADC_WORK,
				   RK3308_ADC_CH1_ADC_INIT |
				   RK3308_ADC_CH2_ADC_INIT);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 2 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				   RK3308_ADC_CH1_ALC_WORK |
				   RK3308_ADC_CH2_ALC_WORK,
				   RK3308_ADC_CH1_ALC_INIT |
				   RK3308_ADC_CH2_ALC_INIT);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 3 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_MIC_WORK |
				   RK3308_ADC_CH2_MIC_WORK,
				   RK3308_ADC_CH1_MIC_INIT |
				   RK3308_ADC_CH2_MIC_INIT);
	}

	usleep_range(200, 250);	/* estimated value */

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 1 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_ADC_WORK |
				   RK3308_ADC_CH2_ADC_WORK,
				   RK3308_ADC_CH1_ADC_WORK |
				   RK3308_ADC_CH2_ADC_WORK);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 2 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				   RK3308_ADC_CH1_ALC_WORK |
				   RK3308_ADC_CH2_ALC_WORK,
				   RK3308_ADC_CH1_ALC_WORK |
				   RK3308_ADC_CH2_ALC_WORK);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 3 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_MIC_WORK |
				   RK3308_ADC_CH2_MIC_WORK,
				   RK3308_ADC_CH1_MIC_WORK |
				   RK3308_ADC_CH2_MIC_WORK);
	}

	return 0;
}

static int rk3308_codec_adc_ana_enable(struct rk3308_codec_priv *rk3308,
				       int type)
{
	unsigned int agc_func_en;
	int idx, grp;

	/*
	 * 1. Set the ACODEC_ADC_ANA_CON7[7:6] and ACODEC_ADC_ANA_CON7[5:4],
	 * to select the line-in or microphone as input of ADC
	 *
	 * Note1. Please ignore the step1 for enabling ADC3, ADC4, ADC5,
	 * ADC6, ADC7, and ADC8
	 */
	if (rk3308->adc_grp0_using_linein) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(0),
				   RK3308_ADC_CH1_IN_SEL_MSK |
				   RK3308_ADC_CH2_IN_SEL_MSK,
				   RK3308_ADC_CH1_IN_LINEIN |
				   RK3308_ADC_CH2_IN_LINEIN);

		/* Keep other ADCs as MIC-IN */
		for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
			/* The groups without line-in are >= 1 */
			if (grp < 1 || grp > ADC_LR_GROUP_MAX - 1)
				continue;

			regmap_update_bits(rk3308->regmap,
					   RK3308_ADC_ANA_CON07(grp),
					   RK3308_ADC_CH1_IN_SEL_MSK |
					   RK3308_ADC_CH2_IN_SEL_MSK,
					   RK3308_ADC_CH1_IN_MIC |
					   RK3308_ADC_CH2_IN_MIC);
		}
	} else {
		for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
			if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
				continue;

			regmap_update_bits(rk3308->regmap,
					   RK3308_ADC_ANA_CON07(grp),
					   RK3308_ADC_CH1_IN_SEL_MSK |
					   RK3308_ADC_CH2_IN_SEL_MSK,
					   RK3308_ADC_CH1_IN_MIC |
					   RK3308_ADC_CH2_IN_MIC);
		}
	}

	/*
	 * 2. Set ACODEC_ADC_ANA_CON0[7] and [3] to 0x1, to end the mute station
	 * of ADC, to enable the MIC module, to enable the reference voltage
	 * buffer, and to end the initialization of MIC
	 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_MIC_UNMUTE |
				   RK3308_ADC_CH2_MIC_UNMUTE,
				   RK3308_ADC_CH1_MIC_UNMUTE |
				   RK3308_ADC_CH2_MIC_UNMUTE);
	}

	/*
	 * 3. Set ACODEC_ADC_ANA_CON6[0] to 0x1, to enable the current source
	 * of audio
	 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON06(grp),
				   RK3308_ADC_CURRENT_MSK,
				   RK3308_ADC_CURRENT_EN);
	}

	/*
	 * This is mainly used for BIST mode that wait ADCs are stable.
	 *
	 * By tested results, the type delay is >40us, but we need to leave
	 * enough delay margin.
	 */
	usleep_range(400, 500);

	/* vendor step 4*/
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_BUF_REF_EN |
				   RK3308_ADC_CH2_BUF_REF_EN,
				   RK3308_ADC_CH1_BUF_REF_EN |
				   RK3308_ADC_CH2_BUF_REF_EN);
	}

	/* vendor step 5 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_MIC_EN |
				   RK3308_ADC_CH2_MIC_EN,
				   RK3308_ADC_CH1_MIC_EN |
				   RK3308_ADC_CH2_MIC_EN);
	}

	/* vendor step 6 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				   RK3308_ADC_CH1_ALC_EN |
				   RK3308_ADC_CH2_ALC_EN,
				   RK3308_ADC_CH1_ALC_EN |
				   RK3308_ADC_CH2_ALC_EN);
	}

	/* vendor step 7 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_CLK_EN |
				   RK3308_ADC_CH2_CLK_EN,
				   RK3308_ADC_CH1_CLK_EN |
				   RK3308_ADC_CH2_CLK_EN);
	}

	/* vendor step 8 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_ADC_EN |
				   RK3308_ADC_CH2_ADC_EN,
				   RK3308_ADC_CH1_ADC_EN |
				   RK3308_ADC_CH2_ADC_EN);
	}

	/* vendor step 9 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_ADC_WORK |
				   RK3308_ADC_CH2_ADC_WORK,
				   RK3308_ADC_CH1_ADC_WORK |
				   RK3308_ADC_CH2_ADC_WORK);
	}

	/* vendor step 10 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				   RK3308_ADC_CH1_ALC_WORK |
				   RK3308_ADC_CH2_ALC_WORK,
				   RK3308_ADC_CH1_ALC_WORK |
				   RK3308_ADC_CH2_ALC_WORK);
	}

	/* vendor step 11 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_MIC_WORK |
				   RK3308_ADC_CH2_MIC_WORK,
				   RK3308_ADC_CH1_MIC_WORK |
				   RK3308_ADC_CH2_MIC_WORK);
	}

	/* vendor step 12 */

	/* vendor step 13 */

	/* vendor step 14 */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_read(rk3308->regmap, RK3308_ALC_L_DIG_CON09(grp),
			    &agc_func_en);
		if (rk3308->adc_zerocross ||
		    agc_func_en & RK3308_AGC_FUNC_SEL_EN) {
			regmap_update_bits(rk3308->regmap,
					   RK3308_ADC_ANA_CON02(grp),
					   RK3308_ADC_CH1_ZEROCROSS_DET_EN,
					   RK3308_ADC_CH1_ZEROCROSS_DET_EN);
		}
		regmap_read(rk3308->regmap, RK3308_ALC_R_DIG_CON09(grp),
			    &agc_func_en);
		if (rk3308->adc_zerocross ||
		    agc_func_en & RK3308_AGC_FUNC_SEL_EN) {
			regmap_update_bits(rk3308->regmap,
					   RK3308_ADC_ANA_CON02(grp),
					   RK3308_ADC_CH2_ZEROCROSS_DET_EN,
					   RK3308_ADC_CH2_ZEROCROSS_DET_EN);
		}
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		rk3308->adc_grps_endisable[grp] = true;
	}

	return 0;
}

static int rk3308_codec_adc_ana_disable(struct rk3308_codec_priv *rk3308,
					int type)
{
	int idx, grp;

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 1 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				   RK3308_ADC_CH1_ZEROCROSS_DET_EN |
				   RK3308_ADC_CH2_ZEROCROSS_DET_EN,
				   RK3308_ADC_CH1_ZEROCROSS_DET_DIS |
				   RK3308_ADC_CH2_ZEROCROSS_DET_DIS);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 2 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_ADC_EN |
				   RK3308_ADC_CH2_ADC_EN,
				   RK3308_ADC_CH1_ADC_DIS |
				   RK3308_ADC_CH2_ADC_DIS);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 3 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_CLK_EN |
				   RK3308_ADC_CH2_CLK_EN,
				   RK3308_ADC_CH1_CLK_DIS |
				   RK3308_ADC_CH2_CLK_DIS);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 4 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				   RK3308_ADC_CH1_ALC_EN |
				   RK3308_ADC_CH2_ALC_EN,
				   RK3308_ADC_CH1_ALC_DIS |
				   RK3308_ADC_CH2_ALC_DIS);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 5 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_MIC_EN |
				   RK3308_ADC_CH2_MIC_EN,
				   RK3308_ADC_CH1_MIC_DIS |
				   RK3308_ADC_CH2_MIC_DIS);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 6 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_BUF_REF_EN |
				   RK3308_ADC_CH2_BUF_REF_EN,
				   RK3308_ADC_CH1_BUF_REF_DIS |
				   RK3308_ADC_CH2_BUF_REF_DIS);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 7 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON06(grp),
				   RK3308_ADC_CURRENT_MSK,
				   RK3308_ADC_CURRENT_DIS);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 8 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
				   RK3308_ADC_CH1_ADC_WORK |
				   RK3308_ADC_CH2_ADC_WORK,
				   RK3308_ADC_CH1_ADC_INIT |
				   RK3308_ADC_CH2_ADC_INIT);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 9 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				   RK3308_ADC_CH1_ALC_WORK |
				   RK3308_ADC_CH2_ALC_WORK,
				   RK3308_ADC_CH1_ALC_INIT |
				   RK3308_ADC_CH2_ALC_INIT);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		/* vendor step 10 */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
				   RK3308_ADC_CH1_MIC_WORK |
				   RK3308_ADC_CH2_MIC_WORK,
				   RK3308_ADC_CH1_MIC_INIT |
				   RK3308_ADC_CH2_MIC_INIT);
	}

	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		rk3308->adc_grps_endisable[grp] = false;
	}

	return 0;
}

static int rk3308_codec_open_capture(struct rk3308_codec_priv *rk3308)
{
	int idx, grp = 0;
	int type = ADC_TYPE_NORMAL;

	rk3308_codec_adc_ana_enable(rk3308, type);
	rk3308_codec_adc_reinit_mics(rk3308, type);

	if (rk3308->adc_grp0_using_linein) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON03(0),
				   RK3308_ADC_L_CH_BIST_MSK,
				   RK3308_ADC_L_CH_NORMAL_RIGHT);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON03(0),
				   RK3308_ADC_R_CH_BIST_MSK,
				   RK3308_ADC_R_CH_NORMAL_LEFT);
	} else {
		for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
			if (handle_loopback(rk3308) &&
			    idx == rk3308->loopback_grp &&
			    grp == ADC_GRP_SKIP_MAGIC) {
				/*
				 * Switch to dummy BIST mode (BIST keep reset
				 * now) to keep the zero input data in I2S bus.
				 *
				 * It may cause the glitch if we hold the ADC
				 * digtital i2s module in codec.
				 *
				 * Then, the grp which is set from loopback_grp.
				 */
				regmap_update_bits(rk3308->regmap,
						   RK3308_ADC_DIG_CON03(rk3308->loopback_grp),
						   RK3308_ADC_L_CH_BIST_MSK,
						   RK3308_ADC_L_CH_BIST_SINE);
				regmap_update_bits(rk3308->regmap,
						   RK3308_ADC_DIG_CON03(rk3308->loopback_grp),
						   RK3308_ADC_R_CH_BIST_MSK,
						   RK3308_ADC_R_CH_BIST_SINE);
			} else {
				if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
					continue;

				regmap_update_bits(rk3308->regmap,
						   RK3308_ADC_DIG_CON03(grp),
						   RK3308_ADC_L_CH_BIST_MSK,
						   RK3308_ADC_L_CH_NORMAL_LEFT);
				regmap_update_bits(rk3308->regmap,
						   RK3308_ADC_DIG_CON03(grp),
						   RK3308_ADC_R_CH_BIST_MSK,
						   RK3308_ADC_R_CH_NORMAL_RIGHT);
			}
		}
	}

	return 0;
}

static void rk3308_codec_adc_mclk_disable(struct rk3308_codec_priv *rk3308)
{
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_ADC_MCLK_MSK,
			   RK3308_ADC_MCLK_DIS);
}

static void rk3308_codec_adc_mclk_enable(struct rk3308_codec_priv *rk3308)
{
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_ADC_MCLK_MSK,
			   RK3308_ADC_MCLK_EN);
	udelay(20);
}

static void rk3308_codec_dac_mclk_disable(struct rk3308_codec_priv *rk3308)
{
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_DAC_MCLK_MSK,
			   RK3308_DAC_MCLK_DIS);
}

static void rk3308_codec_dac_mclk_enable(struct rk3308_codec_priv *rk3308)
{
	regmap_update_bits(rk3308->regmap, RK3308_GLB_CON,
			   RK3308_DAC_MCLK_MSK,
			   RK3308_DAC_MCLK_EN);
	udelay(20);
}

static int rk3308_codec_open_dbg_capture(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_adc_ana_enable(rk3308, ADC_TYPE_DBG);

	return 0;
}

static int rk3308_codec_close_dbg_capture(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_adc_ana_disable(rk3308, ADC_TYPE_DBG);

	return 0;
}

static int rk3308_codec_close_all_capture(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_adc_ana_disable(rk3308, ADC_TYPE_ALL);

	return 0;
}

static int rk3308_codec_close_capture(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_adc_ana_disable(rk3308, ADC_TYPE_NORMAL);

	return 0;
}

static int rk3308_codec_open_playback(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_dac_enable(rk3308);

	return 0;
}

static int rk3308_codec_close_playback(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_dac_disable(rk3308);

	return 0;
}

static int rk3308_codec_llp_down(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_adc_mclk_disable(rk3308);
	rk3308_codec_dac_mclk_disable(rk3308);

	return 0;
}

static int rk3308_codec_llp_up(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_adc_mclk_enable(rk3308);
	rk3308_codec_dac_mclk_enable(rk3308);

	return 0;
}

static int rk3308_codec_dlp_down(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_micbias_disable(rk3308);
	rk3308_codec_power_off(rk3308);

	return 0;
}

static int rk3308_codec_dlp_up(struct rk3308_codec_priv *rk3308)
{
	rk3308_codec_power_on(rk3308);
	rk3308_codec_micbias_enable(rk3308, rk3308->micbias_volt);

	return 0;
}

/* Just used for debug and trace power state */
static void rk3308_codec_set_pm_state(struct rk3308_codec_priv *rk3308,
				      int pm_state)
{
	int ret;

	switch (pm_state) {
	case PM_LLP_DOWN:
		rk3308_codec_llp_down(rk3308);
		break;
	case PM_LLP_UP:
		rk3308_codec_llp_up(rk3308);
		break;
	case PM_DLP_DOWN:
		rk3308_codec_dlp_down(rk3308);
		break;
	case PM_DLP_UP:
		rk3308_codec_dlp_up(rk3308);
		break;
	case PM_DLP_DOWN2:
		clk_disable_unprepare(rk3308->mclk_rx);
		clk_disable_unprepare(rk3308->mclk_tx);
		clk_disable_unprepare(rk3308->pclk);
		break;
	case PM_DLP_UP2:
		ret = clk_prepare_enable(rk3308->pclk);
		if (ret < 0) {
			dev_err(rk3308->plat_dev,
				"Failed to enable acodec pclk: %d\n", ret);
			goto err;
		}

		ret = clk_prepare_enable(rk3308->mclk_rx);
		if (ret < 0) {
			dev_err(rk3308->plat_dev,
				"Failed to enable i2s mclk_rx: %d\n", ret);
			goto err;
		}

		ret = clk_prepare_enable(rk3308->mclk_tx);
		if (ret < 0) {
			dev_err(rk3308->plat_dev,
				"Failed to enable i2s mclk_tx: %d\n", ret);
			goto err;
		}
		break;
	default:
		dev_err(rk3308->plat_dev, "Invalid pm_state: %d\n", pm_state);
		goto err;
	}

	rk3308->pm_state = pm_state;

err:
	return;
}

static void rk3308_codec_update_adcs_status(struct rk3308_codec_priv *rk3308,
					    int state)
{
	int idx, grp;

	/* Update skip_grps flags if the ADCs need to be enabled always. */
	if (state == PATH_BUSY) {
		for (idx = 0; idx < rk3308->used_adc_grps; idx++) {
			u32 mapped_grp = to_mapped_grp(rk3308, idx);

			for (grp = 0; grp < rk3308->en_always_grps_num; grp++) {
				u32 en_always_grp = rk3308->en_always_grps[grp];

				if (mapped_grp == en_always_grp)
					rk3308->skip_grps[en_always_grp] = 1;
			}
		}
	}
}

static int rk3308_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	struct snd_pcm_str *playback_str =
			&substream->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK];
	int type = ADC_TYPE_LOOPBACK;
	int idx, grp;
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* DAC only supports 2 channels */
		rk3308_codec_dac_mclk_enable(rk3308);
		rk3308_codec_open_playback(rk3308);
		rk3308_codec_dac_dig_config(rk3308, params);
		rk3308_codec_set_dac_path_state(rk3308, PATH_BUSY);
	} else {
		if (rk3308->micbias_num &&
		    !rk3308->enable_micbias)
			rk3308_codec_micbias_enable(rk3308, rk3308->micbias_volt);

		rk3308_codec_adc_mclk_enable(rk3308);
		ret = rk3308_codec_update_adc_grps(rk3308, params);
		if (ret < 0)
			return ret;

		if (handle_loopback(rk3308)) {
			if (rk3308->micbias_num &&
			    (params_channels(params) == 2) &&
			    to_mapped_grp(rk3308, 0) == rk3308->loopback_grp)
				rk3308_codec_micbias_disable(rk3308);

			/* Check the DACs are opened */
			if (playback_str->substream_opened) {
				rk3308->loopback_dacs_enabled = true;
				for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
					if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
						continue;

					regmap_update_bits(rk3308->regmap,
							   RK3308_ADC_DIG_CON03(grp),
							   RK3308_ADC_L_CH_BIST_MSK,
							   RK3308_ADC_L_CH_NORMAL_LEFT);
					regmap_update_bits(rk3308->regmap,
							   RK3308_ADC_DIG_CON03(grp),
							   RK3308_ADC_R_CH_BIST_MSK,
							   RK3308_ADC_R_CH_NORMAL_RIGHT);
				}
			} else {
				rk3308->loopback_dacs_enabled = false;
				for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
					if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
						continue;

					regmap_update_bits(rk3308->regmap,
							   RK3308_ADC_DIG_CON03(grp),
							   RK3308_ADC_L_CH_BIST_MSK,
							   RK3308_ADC_L_CH_BIST_SINE);
					regmap_update_bits(rk3308->regmap,
							   RK3308_ADC_DIG_CON03(grp),
							   RK3308_ADC_R_CH_BIST_MSK,
							   RK3308_ADC_R_CH_BIST_SINE);
				}
			}
		}

		rk3308_codec_open_capture(rk3308);
		rk3308_codec_adc_dig_config(rk3308, params);
		rk3308_codec_update_adcs_status(rk3308, PATH_BUSY);
	}

	return 0;
}

static int rk3308_pcm_trigger(struct snd_pcm_substream *substream,
			      int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	int type = ADC_TYPE_LOOPBACK;
	int idx, grp;

	if (handle_loopback(rk3308) &&
	    rk3308->dac_output == DAC_LINEOUT &&
	    substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			struct snd_pcm_str *capture_str =
				&substream->pcm->streams[SNDRV_PCM_STREAM_CAPTURE];

			if (capture_str->substream_opened)
				queue_delayed_work(system_power_efficient_wq,
						   &rk3308->loopback_work,
						   msecs_to_jiffies(rk3308->delay_loopback_handle_ms));
		} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
			/*
			 * Switch to dummy bist mode to kick the glitch during disable
			 * ADCs and keep zero input data
			 */
			for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
				if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
					continue;

				regmap_update_bits(rk3308->regmap,
						   RK3308_ADC_DIG_CON03(grp),
						   RK3308_ADC_L_CH_BIST_MSK,
						   RK3308_ADC_L_CH_BIST_SINE);
				regmap_update_bits(rk3308->regmap,
						   RK3308_ADC_DIG_CON03(grp),
						   RK3308_ADC_R_CH_BIST_MSK,
						   RK3308_ADC_R_CH_BIST_SINE);
			}
			rk3308_codec_adc_ana_disable(rk3308, ADC_TYPE_LOOPBACK);
		}
	}

	return 0;
}

static void rk3308_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		rk3308_codec_close_playback(rk3308);
		rk3308_codec_dac_mclk_disable(rk3308);
		regcache_cache_only(rk3308->regmap, false);
		regcache_sync(rk3308->regmap);
		rk3308_codec_set_dac_path_state(rk3308, PATH_IDLE);
	} else {
		rk3308_codec_close_capture(rk3308);
		if (!has_en_always_grps(rk3308)) {
			rk3308_codec_adc_mclk_disable(rk3308);
			rk3308_codec_update_adcs_status(rk3308, PATH_IDLE);
			if (rk3308->micbias_num &&
			    rk3308->enable_micbias)
				rk3308_codec_micbias_disable(rk3308);
		}

		regcache_cache_only(rk3308->regmap, false);
		regcache_sync(rk3308->regmap);
	}
}

static struct snd_soc_dai_ops rk3308_dai_ops = {
	.hw_params = rk3308_hw_params,
	.set_fmt = rk3308_set_dai_fmt,
	.mute_stream = rk3308_mute_stream,
	.trigger = rk3308_pcm_trigger,
	.shutdown = rk3308_pcm_shutdown,
};

static struct snd_soc_dai_driver rk3308_dai[] = {
	{
		.name = "rk3308-hifi",
		.id = RK3308_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rk3308_dai_ops,
	},
};

static int rk3308_suspend(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	if (rk3308->no_deep_low_power)
		goto out;

	rk3308_codec_dlp_down(rk3308);
	clk_disable_unprepare(rk3308->mclk_rx);
	clk_disable_unprepare(rk3308->mclk_tx);
	clk_disable_unprepare(rk3308->pclk);

out:
	rk3308_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rk3308_resume(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	if (rk3308->no_deep_low_power)
		goto out;

	ret = clk_prepare_enable(rk3308->pclk);
	if (ret < 0) {
		dev_err(rk3308->plat_dev,
			"Failed to enable acodec pclk: %d\n", ret);
		goto out;
	}

	ret = clk_prepare_enable(rk3308->mclk_rx);
	if (ret < 0) {
		dev_err(rk3308->plat_dev,
			"Failed to enable i2s mclk_rx: %d\n", ret);
		goto out;
	}

	ret = clk_prepare_enable(rk3308->mclk_tx);
	if (ret < 0) {
		dev_err(rk3308->plat_dev,
			"Failed to enable i2s mclk_tx: %d\n", ret);
		goto out;
	}

	rk3308_codec_dlp_up(rk3308);
out:
	rk3308_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return ret;
}

static int rk3308_codec_default_gains(struct rk3308_codec_priv *rk3308)
{
	int grp;

	/* Prepare ADC gains */
	/* vendor step 12, set MIC PGA default gains */
	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON01(grp),
				   RK3308_ADC_CH1_MIC_GAIN_MSK |
				   RK3308_ADC_CH2_MIC_GAIN_MSK,
				   RK3308_ADC_CH1_MIC_GAIN_0DB |
				   RK3308_ADC_CH2_MIC_GAIN_0DB);
	}

	/* vendor step 13, set ALC default gains */
	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON03(grp),
				   RK3308_ADC_CH1_ALC_GAIN_MSK,
				   RK3308_ADC_CH1_ALC_GAIN_0DB);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON04(grp),
				   RK3308_ADC_CH2_ALC_GAIN_MSK,
				   RK3308_ADC_CH2_ALC_GAIN_0DB);
	}

	/* Prepare DAC gains */
	/* Step 15, set HPMIX default gains */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_GAIN_MSK |
			   RK3308_DAC_R_HPMIX_GAIN_MSK,
			   RK3308_DAC_L_HPMIX_GAIN_NDB_6 |
			   RK3308_DAC_R_HPMIX_GAIN_NDB_6);

	/* Step 18, set HPOUT default gains */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON05,
			   RK3308_DAC_L_HPOUT_GAIN_MSK,
			   RK3308_DAC_L_HPOUT_GAIN_NDB_39);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON06,
			   RK3308_DAC_R_HPOUT_GAIN_MSK,
			   RK3308_DAC_R_HPOUT_GAIN_NDB_39);

	/* Using the same gain to HPOUT LR channels */
	rk3308->hpout_l_dgain = RK3308_DAC_L_HPOUT_GAIN_NDB_39;

	/* Step 19, set LINEOUT default gains */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_GAIN_MSK |
			   RK3308_DAC_R_LINEOUT_GAIN_MSK,
			   RK3308_DAC_L_LINEOUT_GAIN_NDB_6 |
			   RK3308_DAC_R_LINEOUT_GAIN_NDB_6);

	return 0;
}

static int rk3308_codec_setup_en_always_adcs(struct rk3308_codec_priv *rk3308,
					     struct device_node *np)
{
	int num, ret;

	num = of_count_phandle_with_args(np, "rockchip,en-always-grps", NULL);
	if (num < 0) {
		if (num == -ENOENT) {
			/*
			 * If there is note use 'rockchip,en-always-grps'
			 * property, return 0 is also right.
			 */
			ret = 0;
		} else {
			dev_err(rk3308->plat_dev,
				"Failed to read 'rockchip,adc-grps-route' num: %d\n",
				num);
			ret = num;
		}

		rk3308->en_always_grps_num = 0;
		return ret;
	}

	rk3308->en_always_grps_num = num;

	ret = of_property_read_u32_array(np, "rockchip,en-always-grps",
					 rk3308->en_always_grps, num);
	if (ret < 0) {
		dev_err(rk3308->plat_dev,
			"Failed to read 'rockchip,en-always-grps': %d\n",
			ret);
		return ret;
	}

	/* Clear all of skip_grps flags. */
	for (num = 0; num < ADC_LR_GROUP_MAX; num++)
		rk3308->skip_grps[num] = 0;

	/* The loopback grp should not be enabled always. */
	for (num = 0; num < rk3308->en_always_grps_num; num++) {
		if (rk3308->en_always_grps[num] == rk3308->loopback_grp) {
			dev_err(rk3308->plat_dev,
				"loopback_grp: %d should not be enabled always!\n",
				rk3308->loopback_grp);
			ret = -EINVAL;
			return ret;
		}
	}

	return 0;
}

static int rk3308_codec_dapm_mic_gains(struct rk3308_codec_priv *rk3308)
{
	int ret;

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		ret = snd_soc_add_codec_controls(rk3308->codec,
						 mic_gains_b,
						 ARRAY_SIZE(mic_gains_b));
		if (ret) {
			dev_err(rk3308->plat_dev,
				"%s: add mic_gains_b failed: %d\n",
				__func__, ret);
			return ret;
		}
	} else {
		ret = snd_soc_add_codec_controls(rk3308->codec,
						 mic_gains_a,
						 ARRAY_SIZE(mic_gains_a));
		if (ret) {
			dev_err(rk3308->plat_dev,
				"%s: add mic_gains_a failed: %d\n",
				__func__, ret);
			return ret;
		}
	}

	return 0;
}

static int rk3308_codec_check_micbias(struct rk3308_codec_priv *rk3308,
				      struct device_node *np)
{
	struct device *dev = (struct device *)rk3308->plat_dev;
	int num = 0, ret;

	/* Check internal micbias */
	rk3308->micbias1 =
		of_property_read_bool(np, "rockchip,micbias1");
	if (rk3308->micbias1)
		num++;

	rk3308->micbias2 =
		of_property_read_bool(np, "rockchip,micbias2");
	if (rk3308->micbias2)
		num++;

	rk3308->micbias_volt = RK3308_ADC_MICBIAS_VOLT_0_85; /* by default */
	rk3308->micbias_num = num;

	/* Check external micbias */
	rk3308->ext_micbias = EXT_MICBIAS_NONE;

	rk3308->micbias_en_gpio = devm_gpiod_get_optional(dev,
							  "micbias-en",
							  GPIOD_IN);
	if (!rk3308->micbias_en_gpio) {
		dev_info(dev, "Don't need micbias-en gpio\n");
	} else if (IS_ERR(rk3308->micbias_en_gpio)) {
		ret = PTR_ERR(rk3308->micbias_en_gpio);
		dev_err(dev, "Unable to claim gpio micbias-en\n");
		return ret;
	} else if (gpiod_get_value(rk3308->micbias_en_gpio)) {
		rk3308->ext_micbias = EXT_MICBIAS_FUNC1;
	}

	rk3308->vcc_micbias = devm_regulator_get_optional(dev,
							  "vmicbias");
	if (IS_ERR(rk3308->vcc_micbias)) {
		if (PTR_ERR(rk3308->vcc_micbias) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(dev, "no vmicbias regulator found\n");
	} else {
		ret = regulator_enable(rk3308->vcc_micbias);
		if (ret) {
			dev_err(dev, "Can't enable vmicbias: %d\n", ret);
			return ret;
		}
		rk3308->ext_micbias = EXT_MICBIAS_FUNC2;
	}

	dev_info(dev, "Check ext_micbias: %d\n", rk3308->ext_micbias);

	return 0;
}

static int rk3308_codec_dapm_controls_prepare(struct rk3308_codec_priv *rk3308)
{
	int grp;

	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		rk3308->hpf_cutoff[grp] = 0;
		rk3308->agc_l[grp] = 0;
		rk3308->agc_r[grp] = 0;
		rk3308->agc_asr_l[grp] = AGC_ASR_96KHZ;
		rk3308->agc_asr_r[grp] = AGC_ASR_96KHZ;
	}

	rk3308_codec_dapm_mic_gains(rk3308);

	return 0;
}

static int rk3308_codec_prepare(struct rk3308_codec_priv *rk3308)
{
	/* Clear registers for ADC and DAC */
	rk3308_codec_close_playback(rk3308);
	rk3308_codec_close_all_capture(rk3308);
	rk3308_codec_default_gains(rk3308);
	rk3308_codec_llp_down(rk3308);
	rk3308_codec_dapm_controls_prepare(rk3308);

	return 0;
}

static int rk3308_probe(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	int ext_micbias;

	rk3308->codec = codec;
	rk3308_codec_set_dac_path_state(rk3308, PATH_IDLE);

	rk3308_codec_reset(codec);
	rk3308_codec_power_on(rk3308);

	/* From vendor recommend, disable micbias at first. */
	ext_micbias = rk3308->ext_micbias;
	rk3308->ext_micbias = EXT_MICBIAS_NONE;
	rk3308_codec_micbias_disable(rk3308);
	rk3308->ext_micbias = ext_micbias;

	rk3308_codec_prepare(rk3308);
	if (!rk3308->no_hp_det)
		rk3308_codec_headset_detect_enable(rk3308);

	regcache_cache_only(rk3308->regmap, false);
	regcache_sync(rk3308->regmap);

	return 0;
}

static int rk3308_remove(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	rk3308_headphone_ctl(rk3308, 0);
	rk3308_speaker_ctl(rk3308, 0);
	if (!rk3308->no_hp_det)
		rk3308_codec_headset_detect_disable(rk3308);
	rk3308_codec_micbias_disable(rk3308);
	rk3308_codec_power_off(rk3308);

	rk3308_codec_set_dac_path_state(rk3308, PATH_IDLE);

	regcache_cache_only(rk3308->regmap, false);
	regcache_sync(rk3308->regmap);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_rk3308 = {
	.probe = rk3308_probe,
	.remove = rk3308_remove,
	.suspend = rk3308_suspend,
	.resume = rk3308_resume,
	.set_bias_level = rk3308_set_bias_level,
	.controls = rk3308_codec_dapm_controls,
	.num_controls = ARRAY_SIZE(rk3308_codec_dapm_controls),
};

static const struct reg_default rk3308_codec_reg_defaults[] = {
	{ RK3308_GLB_CON, 0x07 },
};

static bool rk3308_codec_write_read_reg(struct device *dev, unsigned int reg)
{
	/* All registers can be read / write */
	return true;
}

static bool rk3308_codec_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static void rk3308_codec_hpdetect_work(struct work_struct *work)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(work, struct rk3308_codec_priv, hpdet_work.work);
	unsigned int val;
	int need_poll = 0, need_irq = 0;
	int need_report = 0, report_type = 0;
	int dac_output = DAC_LINEOUT;

	if (rk3308->codec_ver == ACODEC_VERSION_B) {
		/* Check headphone plugged/unplugged directly. */
		regmap_read(rk3308->detect_grf,
			    DETECT_GRF_ACODEC_HPDET_STATUS, &val);
		regmap_write(rk3308->detect_grf,
			     DETECT_GRF_ACODEC_HPDET_STATUS_CLR, val);

		if (rk3308->hp_jack_reversed) {
			switch (val) {
			case 0x0:
			case 0x2:
				dac_output = DAC_HPOUT;
				report_type = SND_JACK_HEADPHONE;
				break;
			default:
				break;
			}
		} else {
			switch (val) {
			case 0x1:
				dac_output = DAC_HPOUT;
				report_type = SND_JACK_HEADPHONE;
				break;
			default:
				/* Includes val == 2 or others. */
				break;
			}
		}

		rk3308_codec_dac_switch(rk3308, dac_output);
		if (rk3308->hpdet_jack)
			snd_soc_jack_report(rk3308->hpdet_jack,
					    report_type,
					    SND_JACK_HEADPHONE);

		enable_irq(rk3308->irq);

		return;
	}

	/* Check headphone unplugged via poll. */
	regmap_read(rk3308->regmap, RK3308_DAC_DIG_CON14, &val);

	if (rk3308->hp_jack_reversed) {
		if (!val) {
			rk3308->hp_plugged = true;
			report_type = SND_JACK_HEADPHONE;

			need_report = 1;
			need_irq = 1;
		} else {
			if (rk3308->hp_plugged) {
				rk3308->hp_plugged = false;
				need_report = 1;
			}
			need_poll = 1;
		}
	} else {
		if (!val) {
			rk3308->hp_plugged = false;

			need_report = 1;
			need_irq = 1;
		} else {
			if (!rk3308->hp_plugged) {
				rk3308->hp_plugged = true;
				report_type = SND_JACK_HEADPHONE;
				need_report = 1;
			}
			need_poll = 1;
		}
	}

	if (need_poll)
		queue_delayed_work(system_power_efficient_wq,
				   &rk3308->hpdet_work,
				   msecs_to_jiffies(HPDET_POLL_MS));

	if (need_report) {
		if (report_type)
			dac_output = DAC_HPOUT;

		rk3308_codec_dac_switch(rk3308, dac_output);

		if (rk3308->hpdet_jack)
			snd_soc_jack_report(rk3308->hpdet_jack,
					    report_type,
					    SND_JACK_HEADPHONE);
	}

	if (need_irq)
		enable_irq(rk3308->irq);
}

static void rk3308_codec_loopback_work(struct work_struct *work)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(work, struct rk3308_codec_priv, loopback_work.work);
	int type = ADC_TYPE_LOOPBACK;
	int idx, grp;

	/* Prepare loopback ADCs */
	rk3308_codec_adc_ana_enable(rk3308, type);

	/* Waiting ADCs are stable */
	msleep(ADC_STABLE_MS);

	/* Recover normal mode after enable ADCs */
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++) {
		if (grp < 0 || grp > ADC_LR_GROUP_MAX - 1)
			continue;

		regmap_update_bits(rk3308->regmap,
				   RK3308_ADC_DIG_CON03(grp),
				   RK3308_ADC_L_CH_BIST_MSK,
				   RK3308_ADC_L_CH_NORMAL_LEFT);
		regmap_update_bits(rk3308->regmap,
				   RK3308_ADC_DIG_CON03(grp),
				   RK3308_ADC_R_CH_BIST_MSK,
				   RK3308_ADC_R_CH_NORMAL_RIGHT);
	}
}

static irqreturn_t rk3308_codec_hpdet_isr(int irq, void *data)
{
	struct rk3308_codec_priv *rk3308 = data;

	/*
	 * For the high level irq trigger, disable irq and avoid a lot of
	 * repeated irq handlers entry.
	 */
	disable_irq_nosync(rk3308->irq);
	queue_delayed_work(system_power_efficient_wq,
			   &rk3308->hpdet_work, msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

void (*rk3308_codec_set_jack_detect_cb)(struct snd_soc_codec *codec,
					struct snd_soc_jack *hpdet_jack);
EXPORT_SYMBOL_GPL(rk3308_codec_set_jack_detect_cb);

static void rk3308_codec_set_jack_detect(struct snd_soc_codec *codec,
				  struct snd_soc_jack *hpdet_jack)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	rk3308->hpdet_jack = hpdet_jack;

	/* To detect jack once during startup */
	disable_irq_nosync(rk3308->irq);
	queue_delayed_work(system_power_efficient_wq,
			   &rk3308->hpdet_work, msecs_to_jiffies(10));

	dev_info(rk3308->plat_dev, "%s: Request detect hp jack once\n",
		 __func__);
}

static const struct regmap_config rk3308_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = RK3308_DAC_ANA_CON15,
	.writeable_reg = rk3308_codec_write_read_reg,
	.readable_reg = rk3308_codec_write_read_reg,
	.volatile_reg = rk3308_codec_volatile_reg,
	.reg_defaults = rk3308_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk3308_codec_reg_defaults),
	.cache_type = REGCACHE_FLAT,
};

static ssize_t pm_state_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);

	return sprintf(buf, "pm_state: %d\n", rk3308->pm_state);
}

static ssize_t pm_state_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long pm_state;
	int ret = kstrtoul(buf, 10, &pm_state);

	if (ret < 0) {
		dev_err(dev, "Invalid pm_state: %ld, ret: %d\n",
			pm_state, ret);
		return -EINVAL;
	}

	rk3308_codec_set_pm_state(rk3308, pm_state);

	dev_info(dev, "Store pm_state: %d\n", rk3308->pm_state);

	return count;
}

static ssize_t adc_grps_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	u32 grp;
	int type = ADC_TYPE_NORMAL, count = 0;
	int idx;

	count += sprintf(buf + count, "current used adc_grps:\n");
	count += sprintf(buf + count, "- normal:");
	for (idx = 0; adc_for_each_grp(rk3308, type, idx, &grp); idx++)
		count += sprintf(buf + count, " %d", grp);
	count += sprintf(buf + count, "\n");
	count += sprintf(buf + count, "- loopback: %d\n",
			 rk3308->loopback_grp);

	return count;
}

static ssize_t adc_grps_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	char adc_type;
	int grps, ret;

	ret = sscanf(buf, "%c,%d", &adc_type, &grps);
	if (ret != 2) {
		dev_err(rk3308->plat_dev, "%s sscanf failed: %d\n",
			__func__, ret);
		return -EFAULT;
	}

	if (adc_type == 'n')
		rk3308->used_adc_grps = grps;
	else if (adc_type == 'l')
		rk3308->loopback_grp = grps;

	return count;
}

static ssize_t adc_grps_route_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	char which_i2s[32] = {0};
	int count = 0;
	u32 grp;

	switch (rk3308->which_i2s) {
	case ACODEC_TO_I2S1_2CH:
		strcpy(which_i2s, "i2s1_2ch");
		break;
	case ACODEC_TO_I2S3_4CH:
		strcpy(which_i2s, "i2s3_4ch");
		break;
	default:
		strcpy(which_i2s, "i2s2_8ch");
		break;
	}

	count += sprintf(buf + count, "%s from acodec route mapping:\n",
			 which_i2s);
	for (grp = 0; grp < rk3308->to_i2s_grps; grp++) {
		count += sprintf(buf + count, "* sdi_%d <-- sdo_%d\n",
				 grp, rk3308->i2s_sdis[grp]);
	}

	return count;
}

static ssize_t adc_grps_route_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	int which_i2s, idx, i2s_sdis[ADC_LR_GROUP_MAX];
	int ret;

	ret = sscanf(buf, "%d,%d,%d,%d,%d", &which_i2s,
		     &i2s_sdis[0], &i2s_sdis[1], &i2s_sdis[2], &i2s_sdis[3]);
	if (ret != 5) {
		dev_err(rk3308->plat_dev, "%s sscanf failed: %d\n",
			__func__, ret);
		goto err;
	}

	if (which_i2s < ACODEC_TO_I2S2_8CH ||
	    which_i2s > ACODEC_TO_I2S1_2CH) {
		dev_err(rk3308->plat_dev, "Invalid i2s type: %d\n", which_i2s);
		goto err;
	}

	rk3308->which_i2s = which_i2s;

	switch (rk3308->which_i2s) {
	case ACODEC_TO_I2S1_2CH:
		rk3308->to_i2s_grps = 1;
		break;
	case ACODEC_TO_I2S3_4CH:
		rk3308->to_i2s_grps = 2;
		break;
	default:
		rk3308->to_i2s_grps = 4;
		break;
	}

	for (idx = 0; idx < rk3308->to_i2s_grps; idx++)
		rk3308->i2s_sdis[idx] = i2s_sdis[idx];

	rk3308_codec_adc_grps_route_config(rk3308);

err:
	return count;
}

static ssize_t adc_grp0_in_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);

	return sprintf(buf, "adc ch0 using: %s\n",
		       rk3308->adc_grp0_using_linein ? "line in" : "mic in");
}

static ssize_t adc_grp0_in_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long using_linein;
	int ret = kstrtoul(buf, 10, &using_linein);

	if (ret < 0 || using_linein > 1) {
		dev_err(dev, "Invalid input status: %ld, ret: %d\n",
			using_linein, ret);
		return -EINVAL;
	}

	rk3308->adc_grp0_using_linein = using_linein;

	dev_info(dev, "store using_linein: %d\n",
		 rk3308->adc_grp0_using_linein);

	return count;
}

static ssize_t adc_zerocross_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);

	return sprintf(buf, "adc zerocross: %s\n",
		       rk3308->adc_zerocross ? "enabled" : "disabled");
}

static ssize_t adc_zerocross_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long zerocross;
	int ret = kstrtoul(buf, 10, &zerocross);

	if (ret < 0 || zerocross > 1) {
		dev_err(dev, "Invalid zerocross: %ld, ret: %d\n",
			zerocross, ret);
		return -EINVAL;
	}

	rk3308->adc_zerocross = zerocross;

	dev_info(dev, "store adc zerocross: %d\n", rk3308->adc_zerocross);

	return count;
}

static ssize_t adc_grps_endisable_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	int count = 0, i;

	count += sprintf(buf + count, "enabled adc grps:");
	for (i = 0; i < ADC_LR_GROUP_MAX; i++)
		count += sprintf(buf + count, "%d ",
				 rk3308->adc_grps_endisable[i]);

	count += sprintf(buf + count, "\n");
	return count;
}

static ssize_t adc_grps_endisable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	int grp, endisable, ret;

	ret = sscanf(buf, "%d,%d", &grp, &endisable);
	if (ret != 2) {
		dev_err(rk3308->plat_dev, "%s sscanf failed: %d\n",
			__func__, ret);
		return -EFAULT;
	}

	rk3308->cur_dbg_grp = grp;

	if (endisable)
		rk3308_codec_open_dbg_capture(rk3308);
	else
		rk3308_codec_close_dbg_capture(rk3308);

	dev_info(dev, "ADC grp %d endisable: %d\n", grp, endisable);

	return count;
}

static ssize_t dac_endisable_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);

	return sprintf(buf, "%d\n", rk3308->dac_endisable);
}

static ssize_t dac_endisable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long endisable;
	int ret = kstrtoul(buf, 10, &endisable);

	if (ret < 0) {
		dev_err(dev, "Invalid endisable: %ld, ret: %d\n",
			endisable, ret);
		return -EINVAL;
	}

	if (endisable)
		rk3308_codec_open_playback(rk3308);
	else
		rk3308_codec_close_playback(rk3308);

	dev_info(dev, "DAC endisable: %ld\n", endisable);

	return count;
}

static ssize_t dac_output_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	ssize_t ret = 0;

	switch (rk3308->dac_output) {
	case DAC_LINEOUT:
		ret = sprintf(buf, "dac path: %s\n", "line out");
		break;
	case DAC_HPOUT:
		ret = sprintf(buf, "dac path: %s\n", "hp out");
		break;
	case DAC_LINEOUT_HPOUT:
		ret = sprintf(buf, "dac path: %s\n",
			      "both line out and hp out");
		break;
	default:
		pr_err("Invalid dac path: %d ?\n", rk3308->dac_output);
		break;
	}

	return ret;
}

static ssize_t dac_output_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long dac_output;
	int ret = kstrtoul(buf, 10, &dac_output);

	if (ret < 0) {
		dev_err(dev, "Invalid input status: %ld, ret: %d\n",
			dac_output, ret);
		return -EINVAL;
	}

	rk3308_codec_dac_switch(rk3308, dac_output);

	dev_info(dev, "Store dac_output: %d\n", rk3308->dac_output);

	return count;
}

static ssize_t enable_all_adcs_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);

	return sprintf(buf, "%d\n", rk3308->enable_all_adcs);
}

static ssize_t enable_all_adcs_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long enable;
	int ret = kstrtoul(buf, 10, &enable);

	if (ret < 0) {
		dev_err(dev, "Invalid enable value: %ld, ret: %d\n",
			enable, ret);
		return -EINVAL;
	}

	rk3308->enable_all_adcs = enable;

	return count;
}

static const struct device_attribute acodec_attrs[] = {
	__ATTR_RW(adc_grps),
	__ATTR_RW(adc_grps_endisable),
	__ATTR_RW(adc_grps_route),
	__ATTR_RW(adc_grp0_in),
	__ATTR_RW(adc_zerocross),
	__ATTR_RW(dac_endisable),
	__ATTR_RW(dac_output),
	__ATTR_RW(enable_all_adcs),
	__ATTR_RW(pm_state),
};

static void rk3308_codec_device_release(struct device *dev)
{
	/* Do nothing */
}

static int rk3308_codec_sysfs_init(struct platform_device *pdev,
				   struct rk3308_codec_priv *rk3308)
{
	struct device *dev = &rk3308->dev;
	int i;

	dev->release = rk3308_codec_device_release;
	dev->parent = &pdev->dev;
	set_dev_node(dev, dev_to_node(&pdev->dev));
	dev_set_name(dev, "rk3308-acodec-dev");

	if (device_register(dev)) {
		dev_err(&pdev->dev,
			"Register 'rk3308-acodec-dev' failed\n");
		dev->parent = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(acodec_attrs); i++) {
		if (device_create_file(dev, &acodec_attrs[i])) {
			dev_err(&pdev->dev,
				"Create 'rk3308-acodec-dev' attr failed\n");
			device_unregister(dev);
			return -ENOMEM;
		}
	}

	return 0;
}

#if defined(CONFIG_DEBUG_FS)
static int rk3308_codec_debugfs_reg_show(struct seq_file *s, void *v)
{
	struct rk3308_codec_priv *rk3308 = s->private;
	unsigned int i;
	unsigned int val;

	for (i = RK3308_GLB_CON; i <= RK3308_DAC_ANA_CON13; i += 4) {
		regmap_read(rk3308->regmap, i, &val);
		if (!(i % 16))
			seq_printf(s, "\nR:%04x: ", i);
		seq_printf(s, "%08x ", val);
	}

	seq_puts(s, "\n");

	return 0;
}

static ssize_t rk3308_codec_debugfs_reg_operate(struct file *file,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct rk3308_codec_priv *rk3308 =
		((struct seq_file *)file->private_data)->private;
	unsigned int reg, val;
	char op;
	char kbuf[32];
	int ret;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	ret = sscanf(kbuf, "%c,%x,%x", &op, &reg, &val);
	if (ret != 3) {
		pr_err("sscanf failed: %d\n", ret);
		return -EFAULT;
	}

	if (op == 'w') {
		pr_info("Write reg: 0x%04x with val: 0x%08x\n", reg, val);
		regmap_write(rk3308->regmap, reg, val);
		regcache_cache_only(rk3308->regmap, false);
		regcache_sync(rk3308->regmap);
		pr_info("Read back reg: 0x%04x with val: 0x%08x\n", reg, val);
	} else if (op == 'r') {
		regmap_read(rk3308->regmap, reg, &val);
		pr_info("Read reg: 0x%04x with val: 0x%08x\n", reg, val);
	} else {
		pr_err("This is an invalid operation: %c\n", op);
	}

	return count;
}

static int rk3308_codec_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file,
			   rk3308_codec_debugfs_reg_show, inode->i_private);
}

static const struct file_operations rk3308_codec_reg_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rk3308_codec_debugfs_open,
	.read = seq_read,
	.write = rk3308_codec_debugfs_reg_operate,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* CONFIG_DEBUG_FS */

static int rk3308_codec_get_version(struct rk3308_codec_priv *rk3308)
{
	unsigned int chip_id;

	regmap_read(rk3308->grf, GRF_CHIP_ID, &chip_id);
	switch (chip_id) {
	case 3306:
		rk3308->codec_ver = ACODEC_VERSION_A;
		break;
	case 0x3308:
		rk3308->codec_ver = ACODEC_VERSION_B;
		break;
	default:
		pr_err("Unknown chip_id: %d / 0x%x\n", chip_id, chip_id);
		return -EFAULT;
	}

	pr_info("The acodec version is: %x\n", rk3308->codec_ver);
	return 0;
}

static int rk3308_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk3308_codec_priv *rk3308;
	struct resource *res;
	void __iomem *base;
	int ret;

	rk3308 = devm_kzalloc(&pdev->dev, sizeof(*rk3308), GFP_KERNEL);
	if (!rk3308)
		return -ENOMEM;

	rk3308->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(rk3308->grf)) {
		dev_err(&pdev->dev,
			"Missing 'rockchip,grf' property\n");
		return PTR_ERR(rk3308->grf);
	}

	ret = rk3308_codec_sysfs_init(pdev, rk3308);
	if (ret < 0) {
		dev_err(&pdev->dev, "Sysfs init failed\n");
		return ret;
	}

#if defined(CONFIG_DEBUG_FS)
	rk3308->dbg_codec = debugfs_create_dir(CODEC_DRV_NAME, NULL);
	if (IS_ERR(rk3308->dbg_codec))
		dev_err(&pdev->dev,
			"Failed to create debugfs dir for rk3308!\n");
	else
		debugfs_create_file("reg", 0644, rk3308->dbg_codec,
				    rk3308, &rk3308_codec_reg_debugfs_fops);
#endif
	rk3308->plat_dev = &pdev->dev;

	rk3308->reset = devm_reset_control_get(&pdev->dev, "acodec-reset");
	if (IS_ERR(rk3308->reset)) {
		ret = PTR_ERR(rk3308->reset);
		if (ret != -ENOENT)
			return ret;

		dev_dbg(&pdev->dev, "No reset control found\n");
		rk3308->reset = NULL;
	}

	rk3308->hp_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "hp-ctl",
						       GPIOD_OUT_LOW);
	if (!rk3308->hp_ctl_gpio) {
		dev_info(&pdev->dev, "Don't need hp-ctl gpio\n");
	} else if (IS_ERR(rk3308->hp_ctl_gpio)) {
		ret = PTR_ERR(rk3308->hp_ctl_gpio);
		dev_err(&pdev->dev, "Unable to claim gpio hp-ctl\n");
		return ret;
	}

	rk3308->spk_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "spk-ctl",
						       GPIOD_OUT_LOW);

	if (!rk3308->spk_ctl_gpio) {
		dev_info(&pdev->dev, "Don't need spk-ctl gpio\n");
	} else if (IS_ERR(rk3308->spk_ctl_gpio)) {
		ret = PTR_ERR(rk3308->spk_ctl_gpio);
		dev_err(&pdev->dev, "Unable to claim gpio spk-ctl\n");
		return ret;
	}

	rk3308->pa_drv_gpio = devm_gpiod_get_optional(&pdev->dev, "pa-drv",
						       GPIOD_OUT_LOW);

	if (!rk3308->pa_drv_gpio) {
		dev_info(&pdev->dev, "Don't need pa-drv gpio\n");
	} else if (IS_ERR(rk3308->pa_drv_gpio)) {
		ret = PTR_ERR(rk3308->pa_drv_gpio);
		dev_err(&pdev->dev, "Unable to claim gpio pa-drv\n");
		return ret;
	}

	if (rk3308->pa_drv_gpio) {
		rk3308->delay_pa_drv_ms = PA_DRV_MS;
		ret = of_property_read_u32(np, "rockchip,delay-pa-drv-ms",
					   &rk3308->delay_pa_drv_ms);
	}

#if DEBUG_POP_ALWAYS
	dev_info(&pdev->dev, "Enable all ctl gpios always for debugging pop\n");
	rk3308_headphone_ctl(rk3308, 1);
	rk3308_speaker_ctl(rk3308, 1);
#else
	dev_info(&pdev->dev, "De-pop as much as possible\n");
	rk3308_headphone_ctl(rk3308, 0);
	rk3308_speaker_ctl(rk3308, 0);
#endif

	rk3308->pclk = devm_clk_get(&pdev->dev, "acodec");
	if (IS_ERR(rk3308->pclk)) {
		dev_err(&pdev->dev, "Can't get acodec pclk\n");
		return PTR_ERR(rk3308->pclk);
	}

	rk3308->mclk_rx = devm_clk_get(&pdev->dev, "mclk_rx");
	if (IS_ERR(rk3308->mclk_rx)) {
		dev_err(&pdev->dev, "Can't get acodec mclk_rx\n");
		return PTR_ERR(rk3308->mclk_rx);
	}

	rk3308->mclk_tx = devm_clk_get(&pdev->dev, "mclk_tx");
	if (IS_ERR(rk3308->mclk_tx)) {
		dev_err(&pdev->dev, "Can't get acodec mclk_tx\n");
		return PTR_ERR(rk3308->mclk_tx);
	}

	ret = clk_prepare_enable(rk3308->pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable acodec pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(rk3308->mclk_rx);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable i2s mclk_rx: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(rk3308->mclk_tx);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable i2s mclk_tx: %d\n", ret);
		return ret;
	}

	rk3308_codec_check_micbias(rk3308, np);

	rk3308->enable_all_adcs =
		of_property_read_bool(np, "rockchip,enable-all-adcs");

	rk3308->hp_jack_reversed =
		of_property_read_bool(np, "rockchip,hp-jack-reversed");

	rk3308->no_deep_low_power =
		of_property_read_bool(np, "rockchip,no-deep-low-power");

	rk3308->no_hp_det =
		of_property_read_bool(np, "rockchip,no-hp-det");

	rk3308->delay_loopback_handle_ms = LOOPBACK_HANDLE_MS;
	ret = of_property_read_u32(np, "rockchip,delay-loopback-handle-ms",
				   &rk3308->delay_loopback_handle_ms);

	rk3308->delay_start_play_ms = 0;
	ret = of_property_read_u32(np, "rockchip,delay-start-play-ms",
				   &rk3308->delay_start_play_ms);

	rk3308->loopback_grp = NOT_USED;
	ret = of_property_read_u32(np, "rockchip,loopback-grp",
				   &rk3308->loopback_grp);
	/*
	 * If there is no loopback on some board, the -EINVAL indicates that
	 * we don't need add the node, and it is not an error.
	 */
	if (ret < 0 && ret != -EINVAL) {
		dev_err(&pdev->dev, "Failed to read loopback property: %d\n",
			ret);
		return ret;
	}

	ret = rk3308_codec_adc_grps_route(rk3308, np);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to route ADC groups: %d\n",
			ret);
		return ret;
	}

	ret = rk3308_codec_setup_en_always_adcs(rk3308, np);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to setup enabled always ADCs: %d\n",
			ret);
		return ret;
	}

	ret = rk3308_codec_get_version(rk3308);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get acodec version: %d\n",
			ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		dev_err(&pdev->dev, "Failed to ioremap resource\n");
		goto failed;
	}

	rk3308->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					       &rk3308_codec_regmap_config);
	if (IS_ERR(rk3308->regmap)) {
		ret = PTR_ERR(rk3308->regmap);
		dev_err(&pdev->dev, "Failed to regmap mmio\n");
		goto failed;
	}

	if (!rk3308->no_hp_det) {
		int index = 0;

		if (rk3308->codec_ver == ACODEC_VERSION_B)
			index = 1;

		rk3308->irq = platform_get_irq(pdev, index);
		if (rk3308->irq < 0) {
			dev_err(&pdev->dev, "Can not get codec irq\n");
			goto failed;
		}

		INIT_DELAYED_WORK(&rk3308->hpdet_work, rk3308_codec_hpdetect_work);

		ret = devm_request_irq(&pdev->dev, rk3308->irq,
				       rk3308_codec_hpdet_isr,
				       0,
				       "acodec-hpdet",
				       rk3308);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to request IRQ: %d\n", ret);
			goto failed;
		}

		if (rk3308->codec_ver == ACODEC_VERSION_B) {
			rk3308->detect_grf =
				syscon_regmap_lookup_by_phandle(np, "rockchip,detect-grf");
			if (IS_ERR(rk3308->detect_grf)) {
				dev_err(&pdev->dev,
					"Missing 'rockchip,detect-grf' property\n");
				return PTR_ERR(rk3308->detect_grf);
			}

			/* Configure filter count and enable hpdet irq. */
			regmap_write(rk3308->detect_grf,
				     DETECT_GRF_ACODEC_HPDET_COUNTER,
				     DEFAULT_HPDET_COUNT);
			regmap_write(rk3308->detect_grf,
				     DETECT_GRF_ACODEC_HPDET_CON,
				     (HPDET_BOTH_NEG_POS << 16) |
				      HPDET_BOTH_NEG_POS);
		}

		rk3308_codec_set_jack_detect_cb = rk3308_codec_set_jack_detect;
	}

	if (rk3308->codec_ver == ACODEC_VERSION_A)
		INIT_DELAYED_WORK(&rk3308->loopback_work,
				  rk3308_codec_loopback_work);

	rk3308->adc_grp0_using_linein = ADC_GRP0_MICIN;
	rk3308->dac_output = DAC_LINEOUT;
	rk3308->adc_zerocross = 1;
	rk3308->pm_state = PM_NORMAL;

	platform_set_drvdata(pdev, rk3308);

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_rk3308,
				     rk3308_dai, ARRAY_SIZE(rk3308_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register codec: %d\n", ret);
		goto failed;
	}

	return ret;

failed:
	clk_disable_unprepare(rk3308->mclk_rx);
	clk_disable_unprepare(rk3308->mclk_tx);
	clk_disable_unprepare(rk3308->pclk);
	device_unregister(&rk3308->dev);

	return ret;
}

static int rk3308_platform_remove(struct platform_device *pdev)
{
	struct rk3308_codec_priv *rk3308 =
		(struct rk3308_codec_priv *)platform_get_drvdata(pdev);

	clk_disable_unprepare(rk3308->mclk_rx);
	clk_disable_unprepare(rk3308->mclk_tx);
	clk_disable_unprepare(rk3308->pclk);
	snd_soc_unregister_codec(&pdev->dev);
	device_unregister(&rk3308->dev);

	return 0;
}

static const struct of_device_id rk3308codec_of_match[] = {
	{ .compatible = "rockchip,rk3308-codec", },
	{},
};
MODULE_DEVICE_TABLE(of, rk3308codec_of_match);

static struct platform_driver rk3308_codec_driver = {
	.driver = {
		   .name = CODEC_DRV_NAME,
		   .of_match_table = of_match_ptr(rk3308codec_of_match),
	},
	.probe = rk3308_platform_probe,
	.remove = rk3308_platform_remove,
};
module_platform_driver(rk3308_codec_driver);

MODULE_AUTHOR("Xing Zheng <zhengxing@rock-chips.com>");
MODULE_DESCRIPTION("ASoC RK3308 Codec Driver");
MODULE_LICENSE("GPL v2");
