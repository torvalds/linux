// SPDX-License-Identifier: GPL-2.0
/*
 *  MediaTek ALSA SoC Audio DAI I2S Control
 *
 *  Copyright (c) 2025 MediaTek Inc.
 *  Author: Darren Ye <darren.ye@mediatek.com>
 */

#include <linux/bitops.h>
#include <linux/regmap.h>

#include <sound/pcm_params.h>

#include "mt8189-afe-clk.h"
#include "mt8189-afe-common.h"
#include "mt8189-interconnection.h"

#include "../common/mtk-afe-fe-dai.h"

#define I2SIN0_MCLK_EN_W_NAME "I2SIN0_MCLK_EN"
#define I2SIN1_MCLK_EN_W_NAME "I2SIN1_MCLK_EN"
#define I2SOUT0_MCLK_EN_W_NAME "I2SOUT0_MCLK_EN"
#define I2SOUT1_MCLK_EN_W_NAME "I2SOUT1_MCLK_EN"
#define I2SOUT4_MCLK_EN_W_NAME "I2SOUT4_MCLK_EN"

enum {
	SUPPLY_SEQ_APLL,
	SUPPLY_SEQ_I2S_MCLK_EN,
	SUPPLY_SEQ_I2S_CG_EN,
	SUPPLY_SEQ_I2S_EN,
};

/* this enum is merely for mtk_afe_i2s_priv declare */
enum {
	DAI_I2SIN0,
	DAI_I2SIN1,
	DAI_I2SOUT0,
	DAI_I2SOUT1,
	DAI_I2SOUT4,
	DAI_I2S_NUM,
};

enum {
	ETDM_CLK_SOURCE_H26M,
	ETDM_CLK_SOURCE_APLL,
	ETDM_CLK_SOURCE_SPDIF,
	ETDM_CLK_SOURCE_HDMI,
	ETDM_CLK_SOURCE_EARC,
	ETDM_CLK_SOURCE_LINEIN,
};

enum {
	ETDM_RELATCH_SEL_H26M,
	ETDM_RELATCH_SEL_APLL,
};

enum {
	ETDM_RATE_8K,
	ETDM_RATE_12K,
	ETDM_RATE_16K,
	ETDM_RATE_24K,
	ETDM_RATE_32K,
	ETDM_RATE_48K,
	ETDM_RATE_64K,
	ETDM_RATE_96K,
	ETDM_RATE_128K,
	ETDM_RATE_192K,
	ETDM_RATE_256K,
	ETDM_RATE_384K,
	ETDM_RATE_11025 = 16,
	ETDM_RATE_22050,
	ETDM_RATE_44100,
	ETDM_RATE_88200,
	ETDM_RATE_176400,
	ETDM_RATE_352800,
};

enum {
	ETDM_CONN_8K,
	ETDM_CONN_11K,
	ETDM_CONN_12K,
	ETDM_CONN_16K = 4,
	ETDM_CONN_22K,
	ETDM_CONN_24K,
	ETDM_CONN_32K = 8,
	ETDM_CONN_44K,
	ETDM_CONN_48K,
	ETDM_CONN_88K = 13,
	ETDM_CONN_96K,
	ETDM_CONN_176K = 17,
	ETDM_CONN_192K,
	ETDM_CONN_352K = 21,
	ETDM_CONN_384K,
};

enum {
	ETDM_WLEN_8_BIT = 0x7,
	ETDM_WLEN_16_BIT = 0xf,
	ETDM_WLEN_32_BIT = 0x1f,
};

enum {
	ETDM_SLAVE_SEL_ETDMIN0_MASTER,
	ETDM_SLAVE_SEL_ETDMIN0_SLAVE,
	ETDM_SLAVE_SEL_ETDMIN1_MASTER,
	ETDM_SLAVE_SEL_ETDMIN1_SLAVE,
	ETDM_SLAVE_SEL_ETDMIN2_MASTER,
	ETDM_SLAVE_SEL_ETDMIN2_SLAVE,
	ETDM_SLAVE_SEL_ETDMIN3_MASTER,
	ETDM_SLAVE_SEL_ETDMIN3_SLAVE,
	ETDM_SLAVE_SEL_ETDMOUT0_MASTER,
	ETDM_SLAVE_SEL_ETDMOUT0_SLAVE,
	ETDM_SLAVE_SEL_ETDMOUT1_MASTER,
	ETDM_SLAVE_SEL_ETDMOUT1_SLAVE,
	ETDM_SLAVE_SEL_ETDMOUT2_MASTER,
	ETDM_SLAVE_SEL_ETDMOUT2_SLAVE,
	ETDM_SLAVE_SEL_ETDMOUT3_MASTER,
	ETDM_SLAVE_SEL_ETDMOUT3_SLAVE,
};

struct mtk_afe_i2s_priv {
	int id;
	int rate; /* for determine which apll to use */
	int low_jitter_en;
	unsigned int i2s_low_power_mask;
	const char *share_property_name;
	int share_i2s_id;

	int mclk_id;
	int mclk_rate;
	int mclk_apll;

	int ch_num;
	int sync;
	int ip_mode;
	int slave_mode;
	int lpbk_mode;
};

static unsigned int get_etdm_wlen(snd_pcm_format_t format)
{
	return snd_pcm_format_physical_width(format) <= 16 ?
		ETDM_WLEN_16_BIT : ETDM_WLEN_32_BIT;
}

static unsigned int get_etdm_lrck_width(snd_pcm_format_t format)
{
	if (snd_pcm_format_physical_width(format) <= 1)
		return 0;

	/* The valid data bit number should be larger than 7 due to hardware limitation. */
	return snd_pcm_format_physical_width(format) - 1;
}

static unsigned int get_etdm_rate(unsigned int rate)
{
	switch (rate) {
	case 8000:
		return ETDM_RATE_8K;
	case 12000:
		return ETDM_RATE_12K;
	case 16000:
		return ETDM_RATE_16K;
	case 24000:
		return ETDM_RATE_24K;
	case 32000:
		return ETDM_RATE_32K;
	case 48000:
		return ETDM_RATE_48K;
	case 64000:
		return ETDM_RATE_64K;
	case 96000:
		return ETDM_RATE_96K;
	case 128000:
		return ETDM_RATE_128K;
	case 192000:
		return ETDM_RATE_192K;
	case 256000:
		return ETDM_RATE_256K;
	case 384000:
		return ETDM_RATE_384K;
	case 11025:
		return ETDM_RATE_11025;
	case 22050:
		return ETDM_RATE_22050;
	case 44100:
		return ETDM_RATE_44100;
	case 88200:
		return ETDM_RATE_88200;
	case 176400:
		return ETDM_RATE_176400;
	case 352800:
		return ETDM_RATE_352800;
	default:
		return 0;
	}
}

static unsigned int get_etdm_inconn_rate(unsigned int rate)
{
	switch (rate) {
	case 8000:
		return ETDM_CONN_8K;
	case 12000:
		return ETDM_CONN_12K;
	case 16000:
		return ETDM_CONN_16K;
	case 24000:
		return ETDM_CONN_24K;
	case 32000:
		return ETDM_CONN_32K;
	case 48000:
		return ETDM_CONN_48K;
	case 96000:
		return ETDM_CONN_96K;
	case 192000:
		return ETDM_CONN_192K;
	case 384000:
		return ETDM_CONN_384K;
	case 11025:
		return ETDM_CONN_11K;
	case 22050:
		return ETDM_CONN_22K;
	case 44100:
		return ETDM_CONN_44K;
	case 88200:
		return ETDM_CONN_88K;
	case 176400:
		return ETDM_CONN_176K;
	case 352800:
		return ETDM_CONN_352K;
	default:
		return 0;
	}
}

static int get_i2s_id_by_name(struct mtk_base_afe *afe,
			      const char *name)
{
	if (strncmp(name, "I2SIN0", 6) == 0)
		return MT8189_DAI_I2S_IN0;
	else if (strncmp(name, "I2SIN1", 6) == 0)
		return MT8189_DAI_I2S_IN1;
	else if (strncmp(name, "I2SOUT0", 7) == 0)
		return MT8189_DAI_I2S_OUT0;
	else if (strncmp(name, "I2SOUT1", 7) == 0)
		return MT8189_DAI_I2S_OUT1;
	else if (strncmp(name, "I2SOUT4", 7) == 0)
		return MT8189_DAI_I2S_OUT4;
	else
		return -EINVAL;
}

static struct mtk_afe_i2s_priv *get_i2s_priv_by_name(struct mtk_base_afe *afe,
						     const char *name)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_i2s_id_by_name(afe, name);

	if (dai_id < 0)
		return NULL;

	return afe_priv->dai_priv[dai_id];
}

static const char * const etdm_0_3_loopback_texts[] = {
	"etdmin0", "etdmin1", "etdmout0", "etdmout1"
};

static const u32 etdm_loopback_values[] = {
	0, 2, 8, 10
};

static SOC_VALUE_ENUM_SINGLE_DECL(i2sin0_loopback_enum,
				  ETDM_0_3_COWORK_CON1,
				  ETDM_IN0_SDATA0_SEL_SFT,
				  ETDM_IN0_SDATA0_SEL_MASK,
				  etdm_0_3_loopback_texts,
				  etdm_loopback_values);

static SOC_VALUE_ENUM_SINGLE_DECL(i2sin1_loopback_enum,
				  ETDM_0_3_COWORK_CON1,
				  ETDM_IN1_SDATA0_SEL_SFT,
				  ETDM_IN1_SDATA0_SEL_MASK,
				  etdm_0_3_loopback_texts,
				  etdm_loopback_values);

static const struct snd_kcontrol_new mtk_dai_i2s_controls[] = {
	SOC_ENUM("I2SIN0 Loopback", i2sin0_loopback_enum),
	SOC_ENUM("I2SIN1 Loopback", i2sin1_loopback_enum),
};

/*
 * I2S virtual mux to output widget
 * If the I2S interface is required but not connected to an actual codec dai,
 * a Dummy_Widget must be used to establish the connection.
 */
static const char *const i2s_mux_map[] = {
	"Normal", "Dummy_Widget",
};

static int i2s_mux_map_value[] = {
	0, 1,
};

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(i2s_mux_map_enum,
					      SND_SOC_NOPM,
					      0,
					      1,
					      i2s_mux_map,
					      i2s_mux_map_value);

static const struct snd_kcontrol_new i2s_in0_mux_control =
	SOC_DAPM_ENUM("I2S IN0 Select", i2s_mux_map_enum);
static const struct snd_kcontrol_new i2s_in1_mux_control =
	SOC_DAPM_ENUM("I2S IN1 Select", i2s_mux_map_enum);
static const struct snd_kcontrol_new i2s_out0_mux_control =
	SOC_DAPM_ENUM("I2S OUT0 Select", i2s_mux_map_enum);
static const struct snd_kcontrol_new i2s_out1_mux_control =
	SOC_DAPM_ENUM("I2S OUT1 Select", i2s_mux_map_enum);
static const struct snd_kcontrol_new i2s_out4_mux_control =
	SOC_DAPM_ENUM("I2S OUT4 Select", i2s_mux_map_enum);

static const struct snd_kcontrol_new mtk_i2sout0_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL0_CH1", AFE_CONN108_1, I_DL0_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN108_1, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN108_1, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN108_1, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1", AFE_CONN108_1, I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1", AFE_CONN108_1, I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1", AFE_CONN108_1, I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL7_CH1", AFE_CONN108_1, I_DL7_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH1", AFE_CONN108_1, I_DL8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH1", AFE_CONN108_1, I_DL_24CH_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN0_OUT_CH1", AFE_CONN108_0,
				    I_GAIN0_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN108_0,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN108_4,
				    I_PCM_0_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout0_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL0_CH2", AFE_CONN109_1, I_DL0_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN109_1, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN109_1, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN109_1, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2", AFE_CONN109_1, I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2", AFE_CONN109_1, I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2", AFE_CONN109_1, I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL7_CH2", AFE_CONN109_1, I_DL7_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH2", AFE_CONN109_1, I_DL8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH2", AFE_CONN109_1, I_DL_24CH_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN0_OUT_CH2", AFE_CONN109_0,
				    I_GAIN0_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN109_0,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN109_4,
				    I_PCM_0_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH2", AFE_CONN109_4,
				    I_PCM_0_CAP_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout1_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL0_CH1", AFE_CONN110_1, I_DL0_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN110_1, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN110_1, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN110_1, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1", AFE_CONN110_1, I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1", AFE_CONN110_1, I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1", AFE_CONN110_1, I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL7_CH1", AFE_CONN110_1, I_DL7_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH1", AFE_CONN110_1, I_DL8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH1", AFE_CONN110_1, I_DL_24CH_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN0_OUT_CH1", AFE_CONN110_0,
				    I_GAIN0_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN110_0,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN110_4,
				    I_PCM_0_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout1_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL0_CH2", AFE_CONN111_1, I_DL0_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN111_1, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN111_1, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN111_1, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2", AFE_CONN111_1, I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2", AFE_CONN111_1, I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2", AFE_CONN111_1, I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL7_CH2", AFE_CONN111_1, I_DL7_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH2", AFE_CONN111_1, I_DL8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH2", AFE_CONN111_1, I_DL_24CH_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN0_OUT_CH2", AFE_CONN111_0,
				    I_GAIN0_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN111_0,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN111_4,
				    I_PCM_0_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH2", AFE_CONN111_4,
				    I_PCM_0_CAP_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL0_CH1", AFE_CONN116_1, I_DL0_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN116_1, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN116_1, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN116_1, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1", AFE_CONN116_1, I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1", AFE_CONN116_1, I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1", AFE_CONN116_1, I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL7_CH1", AFE_CONN116_1, I_DL7_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH1", AFE_CONN116_1, I_DL8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH1", AFE_CONN116_1, I_DL_24CH_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL24_CH1", AFE_CONN116_2, I_DL24_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN0_OUT_CH1", AFE_CONN116_0,
				    I_GAIN0_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN116_0,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN116_0,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN116_4,
				    I_PCM_0_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_SRC_2_OUT_CH1", AFE_CONN116_6,
				    I_SRC_2_OUT_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL0_CH2", AFE_CONN117_1, I_DL0_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN117_1, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN117_1, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN117_1, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2", AFE_CONN117_1, I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2", AFE_CONN117_1, I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2", AFE_CONN117_1, I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL7_CH2", AFE_CONN117_1, I_DL7_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH2", AFE_CONN117_1, I_DL8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH2", AFE_CONN117_1, I_DL_24CH_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL24_CH2", AFE_CONN117_2, I_DL24_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN0_OUT_CH2", AFE_CONN117_0,
				    I_GAIN0_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN117_0,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN117_0,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN117_4,
				    I_PCM_0_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH2", AFE_CONN117_4,
				    I_PCM_0_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_SRC_2_OUT_CH2", AFE_CONN117_6,
				    I_SRC_2_OUT_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch3_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH3", AFE_CONN118_1, I_DL_24CH_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN118_4,
				    I_PCM_0_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch4_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH4", AFE_CONN119_1, I_DL_24CH_CH4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_0_CAP_CH1", AFE_CONN118_4,
				    I_PCM_0_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch5_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH5", AFE_CONN120_1, I_DL_24CH_CH5, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch6_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH6", AFE_CONN121_1, I_DL_24CH_CH6, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch7_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH7", AFE_CONN122_1, I_DL_24CH_CH7, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2sout4_ch8_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL_24CH_CH8", AFE_CONN123_1, I_DL_24CH_CH8, 1, 0),
};

static int mtk_apll_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (strcmp(w->name, APLL1_W_NAME) == 0)
			mt8189_apll1_enable(afe);
		else
			mt8189_apll2_enable(afe);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (strcmp(w->name, APLL1_W_NAME) == 0)
			mt8189_apll1_disable(afe);
		else
			mt8189_apll2_disable(afe);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_mclk_en_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	i2s_priv = get_i2s_priv_by_name(afe, w->name);
	if (!i2s_priv)
		return -EINVAL;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8189_mck_enable(afe, i2s_priv->mclk_id, i2s_priv->mclk_rate);
		break;
	case SND_SOC_DAPM_POST_PMD:
		i2s_priv->mclk_rate = 0;
		mt8189_mck_disable(afe, i2s_priv->mclk_id);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget mtk_dai_i2s_widgets[] = {
	SND_SOC_DAPM_MIXER("I2SOUT0_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout0_ch1_mix,
			   ARRAY_SIZE(mtk_i2sout0_ch1_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT0_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout0_ch2_mix,
			   ARRAY_SIZE(mtk_i2sout0_ch2_mix)),

	SND_SOC_DAPM_MIXER("I2SOUT1_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout1_ch1_mix,
			   ARRAY_SIZE(mtk_i2sout1_ch1_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT1_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout1_ch2_mix,
			   ARRAY_SIZE(mtk_i2sout1_ch2_mix)),

	SND_SOC_DAPM_MIXER("I2SOUT4_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch1_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch1_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT4_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch2_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch2_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT4_CH3", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch3_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch3_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT4_CH4", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch4_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch4_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT4_CH5", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch5_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch5_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT4_CH6", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch6_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch6_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT4_CH7", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch7_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch7_mix)),
	SND_SOC_DAPM_MIXER("I2SOUT4_CH8", SND_SOC_NOPM, 0, 0,
			   mtk_i2sout4_ch8_mix,
			   ARRAY_SIZE(mtk_i2sout4_ch8_mix)),

	/* i2s en*/
	SND_SOC_DAPM_SUPPLY_S("I2SIN0_EN", SUPPLY_SEQ_I2S_EN,
			      ETDM_IN0_CON0, REG_ETDM_IN_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SIN1_EN", SUPPLY_SEQ_I2S_EN,
			      ETDM_IN1_CON0, REG_ETDM_IN_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SOUT0_EN", SUPPLY_SEQ_I2S_EN,
			      ETDM_OUT0_CON0, OUT_REG_ETDM_OUT_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SOUT1_EN", SUPPLY_SEQ_I2S_EN,
			      ETDM_OUT1_CON0, OUT_REG_ETDM_OUT_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SOUT4_EN", SUPPLY_SEQ_I2S_EN,
			      ETDM_OUT4_CON0, OUT_REG_ETDM_OUT_EN_SFT, 0,
			      NULL, 0),

	/* i2s mclk en */
	SND_SOC_DAPM_SUPPLY_S(I2SIN0_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2SIN1_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2SOUT0_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2SOUT1_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2SOUT4_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* cg */
	SND_SOC_DAPM_SUPPLY_S("I2SOUT0_CG", SUPPLY_SEQ_I2S_CG_EN,
			      AUDIO_TOP_CON2, PDN_ETDM_OUT0_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SOUT1_CG", SUPPLY_SEQ_I2S_CG_EN,
			      AUDIO_TOP_CON2, PDN_ETDM_OUT1_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SOUT4_CG", SUPPLY_SEQ_I2S_CG_EN,
			      AUDIO_TOP_CON2, PDN_ETDM_OUT4_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SIN0_CG", SUPPLY_SEQ_I2S_CG_EN,
			      AUDIO_TOP_CON2, PDN_ETDM_IN0_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2SIN1_CG", SUPPLY_SEQ_I2S_CG_EN,
			      AUDIO_TOP_CON2, PDN_ETDM_IN1_SFT, 1,
			      NULL, 0),

	/* apll */
	SND_SOC_DAPM_SUPPLY_S(APLL1_W_NAME, SUPPLY_SEQ_APLL,
			      SND_SOC_NOPM, 0, 0,
			      mtk_apll_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(APLL2_W_NAME, SUPPLY_SEQ_APLL,
			      SND_SOC_NOPM, 0, 0,
			      mtk_apll_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* allow i2s on without codec on */
	SND_SOC_DAPM_OUTPUT("I2S_DUMMY_OUT"),
	SND_SOC_DAPM_MUX("I2S_OUT0_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s_out0_mux_control),
	SND_SOC_DAPM_MUX("I2S_OUT1_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s_out1_mux_control),
	SND_SOC_DAPM_MUX("I2S_OUT4_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s_out4_mux_control),

	SND_SOC_DAPM_INPUT("I2S_DUMMY_IN"),
	SND_SOC_DAPM_MUX("I2S_IN0_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s_in0_mux_control),
	SND_SOC_DAPM_MUX("I2S_IN1_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s_in1_mux_control),
};

static int mtk_afe_i2s_share_connect(struct snd_soc_dapm_widget *source,
				     struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(sink->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, sink->name);
	if (!i2s_priv)
		return 0;

	if (i2s_priv->share_i2s_id < 0)
		return 0;

	return i2s_priv->share_i2s_id == get_i2s_id_by_name(afe, source->name);
}

static int mtk_afe_i2s_apll_connect(struct snd_soc_dapm_widget *source,
				    struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(sink->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;
	int cur_apll;
	int needed_apll;

	i2s_priv = get_i2s_priv_by_name(afe, sink->name);
	if (!i2s_priv)
		return 0;

	/* which apll */
	cur_apll = mt8189_get_apll_by_name(afe, source->name);

	/* choose APLL from i2s rate */
	needed_apll = mt8189_get_apll_by_rate(afe, i2s_priv->rate);

	return needed_apll == cur_apll;
}

static int mtk_afe_i2s_mclk_connect(struct snd_soc_dapm_widget *source,
				    struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(sink->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;
	int i2s_num;

	i2s_priv = get_i2s_priv_by_name(afe, sink->name);
	if (!i2s_priv)
		return 0;

	i2s_num = get_i2s_id_by_name(afe, source->name);
	if (get_i2s_id_by_name(afe, sink->name) == i2s_num)
		return i2s_priv->mclk_rate > 0;

	/* check if share i2s need mclk */
	if (i2s_priv->share_i2s_id < 0)
		return 0;

	if (i2s_priv->share_i2s_id == i2s_num)
		return i2s_priv->mclk_rate > 0;

	return 0;
}

static int mtk_afe_mclk_apll_connect(struct snd_soc_dapm_widget *source,
				     struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;
	int cur_apll;

	i2s_priv = get_i2s_priv_by_name(afe, w->name);
	if (!i2s_priv)
		return 0;

	/* which apll */
	cur_apll = mt8189_get_apll_by_name(afe, source->name);

	return i2s_priv->mclk_apll == cur_apll;
}

static const struct snd_soc_dapm_route mtk_dai_i2s_routes[] = {
	/* I2SIN0 */
	{"I2SIN0", NULL, "I2SIN0_EN"},
	{"I2SIN0", NULL, "I2SIN1_EN", mtk_afe_i2s_share_connect},
	{"I2SIN0", NULL, "I2SOUT0_EN", mtk_afe_i2s_share_connect},
	{"I2SIN0", NULL, "I2SOUT1_EN", mtk_afe_i2s_share_connect},
	{"I2SIN0", NULL, "I2SOUT4_EN", mtk_afe_i2s_share_connect},

	{"I2SIN0", NULL, I2SIN0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN0", NULL, I2SIN1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN0", NULL, I2SOUT0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN0", NULL, I2SOUT1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN0", NULL, I2SOUT4_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2SIN0_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2SIN0_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},
	{"I2SIN0", NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SIN0", NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SIN0", NULL, "I2SOUT0_CG"},
	{"I2SIN0", NULL, "I2SIN0_CG"},

	/* i2sin1 */
	{"I2SIN1", NULL, "I2SIN0_EN", mtk_afe_i2s_share_connect},
	{"I2SIN1", NULL, "I2SIN1_EN"},
	{"I2SIN1", NULL, "I2SOUT0_EN", mtk_afe_i2s_share_connect},
	{"I2SIN1", NULL, "I2SOUT1_EN", mtk_afe_i2s_share_connect},
	{"I2SIN1", NULL, "I2SOUT4_EN", mtk_afe_i2s_share_connect},

	{"I2SIN1", NULL, I2SIN0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN1", NULL, I2SIN1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN1", NULL, I2SOUT0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN1", NULL, I2SOUT1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SIN1", NULL, I2SOUT4_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2SIN1_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2SIN1_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},
	{"I2SIN1", NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SIN1", NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SIN1", NULL, "I2SIN1_CG"},
	{"I2SIN1", NULL, "I2SOUT1_CG"},

	/* i2sout0 */
	{"I2SOUT0_CH1", "DL0_CH1", "DL0"},
	{"I2SOUT0_CH2", "DL0_CH2", "DL0"},
	{"I2SOUT0_CH1", "DL1_CH1", "DL1"},
	{"I2SOUT0_CH2", "DL1_CH2", "DL1"},
	{"I2SOUT0_CH1", "DL2_CH1", "DL2"},
	{"I2SOUT0_CH2", "DL2_CH2", "DL2"},
	{"I2SOUT0_CH1", "DL3_CH1", "DL3"},
	{"I2SOUT0_CH2", "DL3_CH2", "DL3"},
	{"I2SOUT0_CH1", "DL4_CH1", "DL4"},
	{"I2SOUT0_CH2", "DL4_CH2", "DL4"},
	{"I2SOUT0_CH1", "DL5_CH1", "DL5"},
	{"I2SOUT0_CH2", "DL5_CH2", "DL5"},
	{"I2SOUT0_CH1", "DL6_CH1", "DL6"},
	{"I2SOUT0_CH2", "DL6_CH2", "DL6"},
	{"I2SOUT0_CH1", "DL7_CH1", "DL7"},
	{"I2SOUT0_CH2", "DL7_CH2", "DL7"},
	{"I2SOUT0_CH1", "DL8_CH1", "DL8"},
	{"I2SOUT0_CH2", "DL8_CH2", "DL8"},
	{"I2SOUT0_CH1", "DL_24CH_CH1", "DL_24CH"},
	{"I2SOUT0_CH2", "DL_24CH_CH2", "DL_24CH"},

	{"I2SOUT0", NULL, "I2SOUT0_CH1"},
	{"I2SOUT0", NULL, "I2SOUT0_CH2"},

	{"I2SOUT0", NULL, "I2SIN0_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT0", NULL, "I2SIN1_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT0", NULL, "I2SOUT0_EN"},
	{"I2SOUT0", NULL, "I2SOUT1_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT0", NULL, "I2SOUT4_EN", mtk_afe_i2s_share_connect},

	{"I2SOUT0", NULL, I2SIN0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT0", NULL, I2SIN1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT0", NULL, I2SOUT0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT0", NULL, I2SOUT1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT0", NULL, I2SOUT4_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2SOUT0_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2SOUT0_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},
	{"I2SOUT0", NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SOUT0", NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SOUT0", NULL, "I2SOUT0_CG"},
	{"I2SOUT0", NULL, "I2SIN0_CG"},

	/* i2sout1 */
	{"I2SOUT1_CH1", "DL0_CH1", "DL0"},
	{"I2SOUT1_CH2", "DL0_CH2", "DL0"},
	{"I2SOUT1_CH1", "DL1_CH1", "DL1"},
	{"I2SOUT1_CH2", "DL1_CH2", "DL1"},
	{"I2SOUT1_CH1", "DL2_CH1", "DL2"},
	{"I2SOUT1_CH2", "DL2_CH2", "DL2"},
	{"I2SOUT1_CH1", "DL3_CH1", "DL3"},
	{"I2SOUT1_CH2", "DL3_CH2", "DL3"},
	{"I2SOUT1_CH1", "DL4_CH1", "DL4"},
	{"I2SOUT1_CH2", "DL4_CH2", "DL4"},
	{"I2SOUT1_CH1", "DL5_CH1", "DL5"},
	{"I2SOUT1_CH2", "DL5_CH2", "DL5"},
	{"I2SOUT1_CH1", "DL6_CH1", "DL6"},
	{"I2SOUT1_CH2", "DL6_CH2", "DL6"},
	{"I2SOUT1_CH1", "DL7_CH1", "DL7"},
	{"I2SOUT1_CH2", "DL7_CH2", "DL7"},
	{"I2SOUT1_CH1", "DL8_CH1", "DL8"},
	{"I2SOUT1_CH2", "DL8_CH2", "DL8"},
	{"I2SOUT1_CH1", "DL_24CH_CH1", "DL_24CH"},
	{"I2SOUT1_CH2", "DL_24CH_CH2", "DL_24CH"},

	{"I2SOUT1", NULL, "I2SOUT1_CH1"},
	{"I2SOUT1", NULL, "I2SOUT1_CH2"},

	{"I2SOUT1", NULL, "I2SIN0_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT1", NULL, "I2SIN1_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT1", NULL, "I2SOUT0_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT1", NULL, "I2SOUT1_EN"},
	{"I2SOUT1", NULL, "I2SOUT4_EN", mtk_afe_i2s_share_connect},

	{"I2SOUT1", NULL, I2SIN0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT1", NULL, I2SIN1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT1", NULL, I2SOUT0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT1", NULL, I2SOUT1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT1", NULL, I2SOUT4_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2SOUT1_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2SOUT1_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},
	{"I2SOUT1", NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SOUT1", NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SOUT1", NULL, "I2SOUT1_CG"},
	{"I2SOUT1", NULL, "I2SIN1_CG"},

	/* i2sout4 */
	{"I2SOUT4_CH1", "DL0_CH1", "DL0"},
	{"I2SOUT4_CH2", "DL0_CH2", "DL0"},
	{"I2SOUT4_CH1", "DL1_CH1", "DL1"},
	{"I2SOUT4_CH2", "DL1_CH2", "DL1"},
	{"I2SOUT4_CH1", "DL2_CH1", "DL2"},
	{"I2SOUT4_CH2", "DL2_CH2", "DL2"},
	{"I2SOUT4_CH1", "DL3_CH1", "DL3"},
	{"I2SOUT4_CH2", "DL3_CH2", "DL3"},
	{"I2SOUT4_CH1", "DL4_CH1", "DL4"},
	{"I2SOUT4_CH2", "DL4_CH2", "DL4"},
	{"I2SOUT4_CH1", "DL5_CH1", "DL5"},
	{"I2SOUT4_CH2", "DL5_CH2", "DL5"},
	{"I2SOUT4_CH1", "DL6_CH1", "DL6"},
	{"I2SOUT4_CH2", "DL6_CH2", "DL6"},
	{"I2SOUT4_CH1", "DL7_CH1", "DL7"},
	{"I2SOUT4_CH2", "DL7_CH2", "DL7"},
	{"I2SOUT4_CH1", "DL8_CH1", "DL8"},
	{"I2SOUT4_CH2", "DL8_CH2", "DL8"},
	{"I2SOUT4_CH1", "DL_24CH_CH1", "DL_24CH"},
	{"I2SOUT4_CH2", "DL_24CH_CH2", "DL_24CH"},
	{"I2SOUT4_CH3", "DL_24CH_CH3", "DL_24CH"},
	{"I2SOUT4_CH4", "DL_24CH_CH4", "DL_24CH"},
	{"I2SOUT4_CH5", "DL_24CH_CH5", "DL_24CH"},
	{"I2SOUT4_CH6", "DL_24CH_CH6", "DL_24CH"},
	{"I2SOUT4_CH7", "DL_24CH_CH7", "DL_24CH"},
	{"I2SOUT4_CH8", "DL_24CH_CH8", "DL_24CH"},
	{"I2SOUT4_CH1", "DL24_CH1", "DL24"},
	{"I2SOUT4_CH2", "DL24_CH2", "DL24"},

	{"I2SOUT4", NULL, "I2SOUT4_CH1"},
	{"I2SOUT4", NULL, "I2SOUT4_CH2"},
	{"I2SOUT4", NULL, "I2SOUT4_CH3"},
	{"I2SOUT4", NULL, "I2SOUT4_CH4"},
	{"I2SOUT4", NULL, "I2SOUT4_CH5"},
	{"I2SOUT4", NULL, "I2SOUT4_CH6"},
	{"I2SOUT4", NULL, "I2SOUT4_CH7"},
	{"I2SOUT4", NULL, "I2SOUT4_CH8"},

	{"I2SOUT4", NULL, "I2SIN0_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT4", NULL, "I2SIN1_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT4", NULL, "I2SOUT0_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT4", NULL, "I2SOUT1_EN", mtk_afe_i2s_share_connect},
	{"I2SOUT4", NULL, "I2SOUT4_EN"},

	{"I2SOUT4", NULL, I2SIN0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT4", NULL, I2SIN1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT4", NULL, I2SOUT0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT4", NULL, I2SOUT1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2SOUT4", NULL, I2SOUT4_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2SOUT4_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2SOUT4_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},
	{"I2SOUT4", NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{"I2SOUT4", NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},
	/* CG */
	{"I2SOUT4", NULL, "I2SOUT4_CG"},

	/* allow i2s on without codec on */
	{"I2SIN0", NULL, "I2S_IN0_Mux"},
	{"I2S_IN0_Mux", "Dummy_Widget", "I2S_DUMMY_IN"},

	{"I2SIN1", NULL, "I2S_IN1_Mux"},
	{"I2S_IN1_Mux", "Dummy_Widget", "I2S_DUMMY_IN"},

	{"I2S_OUT0_Mux", "Dummy_Widget", "I2SOUT0"},
	{"I2S_DUMMY_OUT", NULL, "I2S_OUT0_Mux"},

	{"I2S_OUT1_Mux", "Dummy_Widget", "I2SOUT1"},
	{"I2S_DUMMY_OUT", NULL, "I2S_OUT1_Mux"},

	{"I2S_OUT4_Mux", "Dummy_Widget", "I2SOUT4"},
	{"I2S_DUMMY_OUT", NULL, "I2S_OUT4_Mux"},
};

/* i2s dai ops*/
static int mtk_dai_i2s_config(struct mtk_base_afe *afe,
			      struct snd_pcm_hw_params *params,
			      int i2s_id)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv;
	unsigned int rate = params_rate(params);
	snd_pcm_format_t format = params_format(params);
	int ret;

	if (i2s_id >= MT8189_DAI_NUM || i2s_id < 0)
		return -EINVAL;

	i2s_priv = afe_priv->dai_priv[i2s_id];
	if (!i2s_priv)
		return -EINVAL;

	i2s_priv->rate = rate;

	dev_dbg(afe->dev, "%s(), id %d, rate %d, format %d\n",
		__func__, i2s_id, rate, format);

	switch (i2s_id) {
	case MT8189_DAI_I2S_IN0:
		/* ---etdm in --- */
		regmap_update_bits(afe->regmap, ETDM_IN0_CON1,
				   REG_INITIAL_COUNT_MASK_SFT,
				   0x5 << REG_INITIAL_COUNT_SFT);
		/* 3: pad top 5: no pad top */
		regmap_update_bits(afe->regmap, ETDM_IN0_CON1,
				   REG_INITIAL_POINT_MASK_SFT,
				   0x5 << REG_INITIAL_POINT_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON1,
				   REG_LRCK_RESET_MASK_SFT,
				   0x1 << REG_LRCK_RESET_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON2,
				   REG_CLOCK_SOURCE_SEL_MASK_SFT,
				   ETDM_CLK_SOURCE_APLL <<
				   REG_CLOCK_SOURCE_SEL_SFT);
		/* 0: manual 1: auto */
		regmap_update_bits(afe->regmap, ETDM_IN0_CON2,
				   REG_CK_EN_SEL_AUTO_MASK_SFT,
				   0x1 << REG_CK_EN_SEL_AUTO_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON3,
				   REG_FS_TIMING_SEL_MASK_SFT,
				   get_etdm_rate(rate) <<
				   REG_FS_TIMING_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON4,
				   REG_RELATCH_1X_EN_SEL_MASK_SFT,
				   get_etdm_inconn_rate(rate) <<
				   REG_RELATCH_1X_EN_SEL_SFT);

		regmap_update_bits(afe->regmap, ETDM_IN0_CON8,
				   REG_ETDM_USE_AFIFO_MASK_SFT,
				   0x0 << REG_ETDM_USE_AFIFO_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON8,
				   REG_AFIFO_MODE_MASK_SFT,
				   0x0 << REG_AFIFO_MODE_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON9,
				   REG_ALMOST_END_CH_COUNT_MASK_SFT,
				   0x0 << REG_ALMOST_END_CH_COUNT_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON9,
				   REG_ALMOST_END_BIT_COUNT_MASK_SFT,
				   0x0 << REG_ALMOST_END_BIT_COUNT_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON9,
				   REG_OUT2LATCH_TIME_MASK_SFT,
				   0x6 << REG_OUT2LATCH_TIME_SFT);

		/* 5:  TDM Mode */
		regmap_update_bits(afe->regmap, ETDM_IN0_CON0,
				   REG_FMT_MASK_SFT, 0x0 << REG_FMT_SFT);

		/* APLL */
		regmap_update_bits(afe->regmap, ETDM_IN0_CON0,
				   REG_RELATCH_1X_EN_DOMAIN_SEL_MASK_SFT,
				   ETDM_RELATCH_SEL_APLL
				   << REG_RELATCH_1X_EN_DOMAIN_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON0,
				   REG_BIT_LENGTH_MASK_SFT,
				   get_etdm_lrck_width(format) <<
				   REG_BIT_LENGTH_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN0_CON0,
				   REG_WORD_LENGTH_MASK_SFT,
				   get_etdm_wlen(format) <<
				   REG_WORD_LENGTH_SFT);

		/* ---etdm cowork --- */
		regmap_update_bits(afe->regmap, ETDM_0_3_COWORK_CON0,
				   ETDM_IN0_SLAVE_SEL_MASK_SFT,
				   ETDM_SLAVE_SEL_ETDMOUT0_MASTER
				   << ETDM_IN0_SLAVE_SEL_SFT);
		break;
	case MT8189_DAI_I2S_IN1:
		/* ---etdm in --- */
		regmap_update_bits(afe->regmap, ETDM_IN1_CON1,
				   REG_INITIAL_COUNT_MASK_SFT,
				   0x5 << REG_INITIAL_COUNT_SFT);
		/* 3: pad top 5: no pad top */
		regmap_update_bits(afe->regmap, ETDM_IN1_CON1,
				   REG_INITIAL_POINT_MASK_SFT,
				   0x5 << REG_INITIAL_POINT_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON1,
				   REG_LRCK_RESET_MASK_SFT,
				   0x1 << REG_LRCK_RESET_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON2,
				   REG_CLOCK_SOURCE_SEL_MASK_SFT,
				   ETDM_CLK_SOURCE_APLL <<
				   REG_CLOCK_SOURCE_SEL_SFT);
		/* 0: manual 1: auto */
		regmap_update_bits(afe->regmap, ETDM_IN1_CON2,
				   REG_CK_EN_SEL_AUTO_MASK_SFT,
				   0x1 << REG_CK_EN_SEL_AUTO_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON3,
				   REG_FS_TIMING_SEL_MASK_SFT,
				   get_etdm_rate(rate) <<
				   REG_FS_TIMING_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON4,
				   REG_RELATCH_1X_EN_SEL_MASK_SFT,
				   get_etdm_inconn_rate(rate) <<
				   REG_RELATCH_1X_EN_SEL_SFT);

		regmap_update_bits(afe->regmap, ETDM_IN1_CON8,
				   REG_ETDM_USE_AFIFO_MASK_SFT,
				   0x0 << REG_ETDM_USE_AFIFO_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON8,
				   REG_AFIFO_MODE_MASK_SFT,
				   0x0 << REG_AFIFO_MODE_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON9,
				   REG_ALMOST_END_CH_COUNT_MASK_SFT,
				   0x0 << REG_ALMOST_END_CH_COUNT_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON9,
				   REG_ALMOST_END_BIT_COUNT_MASK_SFT,
				   0x0 << REG_ALMOST_END_BIT_COUNT_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON9,
				   REG_OUT2LATCH_TIME_MASK_SFT,
				   0x6 << REG_OUT2LATCH_TIME_SFT);

		/* 5:  TDM Mode */
		regmap_update_bits(afe->regmap, ETDM_IN1_CON0,
				   REG_FMT_MASK_SFT, 0x0 << REG_FMT_SFT);

		/* APLL */
		regmap_update_bits(afe->regmap, ETDM_IN1_CON0,
				   REG_RELATCH_1X_EN_DOMAIN_SEL_MASK_SFT,
				   ETDM_RELATCH_SEL_APLL
				   << REG_RELATCH_1X_EN_DOMAIN_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON0,
				   REG_BIT_LENGTH_MASK_SFT,
				   get_etdm_lrck_width(format) <<
				   REG_BIT_LENGTH_SFT);
		regmap_update_bits(afe->regmap, ETDM_IN1_CON0,
				   REG_WORD_LENGTH_MASK_SFT,
				   get_etdm_wlen(format) <<
				   REG_WORD_LENGTH_SFT);

		/* ---etdm cowork --- */
		regmap_update_bits(afe->regmap, ETDM_0_3_COWORK_CON1,
				   ETDM_IN1_SLAVE_SEL_MASK_SFT,
				   ETDM_SLAVE_SEL_ETDMOUT1_MASTER
				   << ETDM_IN1_SLAVE_SEL_SFT);
		break;
	case MT8189_DAI_I2S_OUT0:
		/* ---etdm out --- */
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON1,
				   OUT_REG_INITIAL_COUNT_MASK_SFT,
				   0x5 << OUT_REG_INITIAL_COUNT_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON1,
				   OUT_REG_INITIAL_POINT_MASK_SFT,
				   0x6 << OUT_REG_INITIAL_POINT_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON1,
				   OUT_REG_LRCK_RESET_MASK_SFT,
				   0x1 << OUT_REG_LRCK_RESET_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON4,
				   OUT_REG_FS_TIMING_SEL_MASK_SFT,
				   get_etdm_rate(rate) <<
				   OUT_REG_FS_TIMING_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON4,
				   OUT_REG_CLOCK_SOURCE_SEL_MASK_SFT,
				   ETDM_CLK_SOURCE_APLL <<
				   OUT_REG_CLOCK_SOURCE_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON4,
				   OUT_REG_RELATCH_EN_SEL_MASK_SFT,
				   get_etdm_inconn_rate(rate) <<
				   OUT_REG_RELATCH_EN_SEL_SFT);
		/* 5:  TDM Mode */
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON0,
				   OUT_REG_FMT_MASK_SFT,
				   0x0 << OUT_REG_FMT_SFT);

		/* APLL */
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON0,
				   OUT_REG_RELATCH_DOMAIN_SEL_MASK_SFT,
				   ETDM_RELATCH_SEL_APLL
				   << OUT_REG_RELATCH_DOMAIN_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON0,
				   OUT_REG_BIT_LENGTH_MASK_SFT,
				   get_etdm_lrck_width(format) <<
				   OUT_REG_BIT_LENGTH_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT0_CON0,
				   OUT_REG_WORD_LENGTH_MASK_SFT,
				   get_etdm_wlen(format) <<
				   OUT_REG_WORD_LENGTH_SFT);

		/* ---etdm cowork --- */
		regmap_update_bits(afe->regmap, ETDM_0_3_COWORK_CON0,
				   ETDM_OUT0_SLAVE_SEL_MASK_SFT,
				   ETDM_SLAVE_SEL_ETDMIN0_MASTER
				   << ETDM_OUT0_SLAVE_SEL_SFT);
		break;
	case MT8189_DAI_I2S_OUT1:
		/* ---etdm out --- */
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON1,
				   OUT_REG_INITIAL_COUNT_MASK_SFT,
				   0x5 << OUT_REG_INITIAL_COUNT_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON1,
				   OUT_REG_INITIAL_POINT_MASK_SFT,
				   0x6 << OUT_REG_INITIAL_POINT_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON1,
				   OUT_REG_LRCK_RESET_MASK_SFT,
				   0x1 << OUT_REG_LRCK_RESET_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON4,
				   OUT_REG_FS_TIMING_SEL_MASK_SFT,
				   get_etdm_rate(rate) <<
				   OUT_REG_FS_TIMING_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON4,
				   OUT_REG_CLOCK_SOURCE_SEL_MASK_SFT,
				   ETDM_CLK_SOURCE_APLL <<
				   OUT_REG_CLOCK_SOURCE_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON4,
				   OUT_REG_RELATCH_EN_SEL_MASK_SFT,
				   get_etdm_inconn_rate(rate) <<
				   OUT_REG_RELATCH_EN_SEL_SFT);
		/* 5:  TDM Mode */
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON0,
				   OUT_REG_FMT_MASK_SFT,
				   0x0 << OUT_REG_FMT_SFT);

		/* APLL */
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON0,
				   OUT_REG_RELATCH_DOMAIN_SEL_MASK_SFT,
				   ETDM_RELATCH_SEL_APLL
				   << OUT_REG_RELATCH_DOMAIN_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON0,
				   OUT_REG_BIT_LENGTH_MASK_SFT,
				   get_etdm_lrck_width(format) <<
				   OUT_REG_BIT_LENGTH_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON0,
				   OUT_REG_WORD_LENGTH_MASK_SFT,
				   get_etdm_wlen(format) <<
				   OUT_REG_WORD_LENGTH_SFT);

		/* ---etdm cowork --- */
		regmap_update_bits(afe->regmap, ETDM_0_3_COWORK_CON0,
				   ETDM_OUT1_SLAVE_SEL_MASK_SFT,
				   ETDM_SLAVE_SEL_ETDMIN1_MASTER
				   << ETDM_OUT1_SLAVE_SEL_SFT);
		break;
	case MT8189_DAI_I2S_OUT4:
		/* ---etdm out --- */
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON1,
				   OUT_REG_INITIAL_COUNT_MASK_SFT,
				   0x5 << OUT_REG_INITIAL_COUNT_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON1,
				   OUT_REG_INITIAL_POINT_MASK_SFT,
				   0x6 << OUT_REG_INITIAL_POINT_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON1,
				   OUT_REG_LRCK_RESET_MASK_SFT,
				   0x1 << OUT_REG_LRCK_RESET_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON4,
				   OUT_REG_FS_TIMING_SEL_MASK_SFT,
				   get_etdm_rate(rate) <<
				   OUT_REG_FS_TIMING_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON4,
				   OUT_REG_CLOCK_SOURCE_SEL_MASK_SFT,
				   ETDM_CLK_SOURCE_APLL <<
				   OUT_REG_CLOCK_SOURCE_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON4,
				   OUT_REG_RELATCH_EN_SEL_MASK_SFT,
				   get_etdm_inconn_rate(rate) <<
				   OUT_REG_RELATCH_EN_SEL_SFT);
		/* 5:  TDM Mode */
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON0,
				   OUT_REG_FMT_MASK_SFT,
				   0x0 << OUT_REG_FMT_SFT);

		/* APLL */
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON0,
				   OUT_REG_RELATCH_DOMAIN_SEL_MASK_SFT,
				   ETDM_RELATCH_SEL_APLL
				   << OUT_REG_RELATCH_DOMAIN_SEL_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON0,
				   OUT_REG_BIT_LENGTH_MASK_SFT,
				   get_etdm_lrck_width(format) <<
				   OUT_REG_BIT_LENGTH_SFT);
		regmap_update_bits(afe->regmap, ETDM_OUT4_CON0,
				   OUT_REG_WORD_LENGTH_MASK_SFT,
				   get_etdm_wlen(format) <<
				   OUT_REG_WORD_LENGTH_SFT);
		break;
	default:
		dev_err(afe->dev, "%s(), id %d not support\n",
			__func__, i2s_id);
		return -EINVAL;
	}

	/* set share i2s */
	if (i2s_priv->share_i2s_id >= 0) {
		ret = mtk_dai_i2s_config(afe, params, i2s_priv->share_i2s_id);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_dai_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	return mtk_dai_i2s_config(afe, params, dai->id);
}

static int mtk_dai_i2s_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv;
	int apll;
	int apll_rate;

	if (dai->id >= MT8189_DAI_NUM || dai->id < 0 ||
	    dir != SND_SOC_CLOCK_OUT)
		return -EINVAL;

	i2s_priv = afe_priv->dai_priv[dai->id];
	if (!i2s_priv)
		return -EINVAL;

	dev_dbg(afe->dev, "%s(), freq %d\n", __func__, freq);

	apll = mt8189_get_apll_by_rate(afe, freq);
	apll_rate = mt8189_get_apll_rate(afe, apll);

	if (freq > apll_rate || apll_rate % freq) {
		dev_err(afe->dev, "%s(), freq %d, apll_rate %d\n",
			__func__, freq, apll_rate);
		return -EINVAL;
	}

	i2s_priv->mclk_rate = freq;
	i2s_priv->mclk_apll = apll;

	if (i2s_priv->share_i2s_id > 0) {
		struct mtk_afe_i2s_priv *share_i2s_priv;

		share_i2s_priv = afe_priv->dai_priv[i2s_priv->share_i2s_id];
		if (!share_i2s_priv)
			return -EINVAL;

		share_i2s_priv->mclk_rate = i2s_priv->mclk_rate;
		share_i2s_priv->mclk_apll = i2s_priv->mclk_apll;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_i2s_ops = {
	.hw_params = mtk_dai_i2s_hw_params,
	.set_sysclk = mtk_dai_i2s_set_sysclk,
};

/* dai driver */
#define MTK_ETDM_RATES (SNDRV_PCM_RATE_8000_192000)
#define MTK_ETDM_FORMATS (SNDRV_PCM_FMTBIT_S8 |\
			  SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

#define MT8189_I2S_DAI(_name, _id, max_ch, dir) \
{ \
	.name = #_name, \
	.id = _id, \
	.dir = { \
		.stream_name = #_name, \
		.channels_min = 1, \
		.channels_max = max_ch, \
		.rates = MTK_ETDM_RATES, \
		.formats = MTK_ETDM_FORMATS, \
	}, \
	.ops = &mtk_dai_i2s_ops, \
}

static struct snd_soc_dai_driver mtk_dai_i2s_driver[] = {
	/* capture */
	MT8189_I2S_DAI(I2SIN0, MT8189_DAI_I2S_IN0, 2, capture),
	MT8189_I2S_DAI(I2SIN1, MT8189_DAI_I2S_IN1, 2, capture),
	/* playback */
	MT8189_I2S_DAI(I2SOUT0, MT8189_DAI_I2S_OUT0, 2, playback),
	MT8189_I2S_DAI(I2SOUT1, MT8189_DAI_I2S_OUT1, 2, playback),
	MT8189_I2S_DAI(I2SOUT4, MT8189_DAI_I2S_OUT4, 8, playback),
};

static const struct mtk_afe_i2s_priv mt8189_i2s_priv[DAI_I2S_NUM] = {
	[DAI_I2SIN0] = {
		.id = MT8189_DAI_I2S_IN0,
		.mclk_id = MT8189_I2SIN0_MCK,
		.share_property_name = "i2sin0-share",
		.share_i2s_id = MT8189_DAI_I2S_OUT0,
	},
	[DAI_I2SIN1] = {
		.id = MT8189_DAI_I2S_IN1,
		.mclk_id = MT8189_I2SIN1_MCK,
		.share_property_name = "i2sin1-share",
		.share_i2s_id = MT8189_DAI_I2S_OUT1,
	},
	[DAI_I2SOUT0] = {
		.id = MT8189_DAI_I2S_OUT0,
		.mclk_id = MT8189_I2SOUT0_MCK,
		.share_property_name = "i2sout0-share",
		.share_i2s_id = -1,
	},
	[DAI_I2SOUT1] = {
		.id = MT8189_DAI_I2S_OUT1,
		.mclk_id = MT8189_I2SOUT1_MCK,
		.share_property_name = "i2sout1-share",
		.share_i2s_id = -1,
	},
	[DAI_I2SOUT4] = {
		.id = MT8189_DAI_I2S_OUT4,
		.mclk_id = MT8189_I2SIN1_MCK,
		.share_property_name = "i2sout4-share",
		.share_i2s_id = -1,
	},
};

static int mt8189_dai_i2s_get_share(struct mtk_base_afe *afe)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	const struct device_node *of_node = afe->dev->of_node;

	for (int i = 0; i < DAI_I2S_NUM; i++) {
		const char *of_str;
		struct mtk_afe_i2s_priv *i2s_priv =
			afe_priv->dai_priv[mt8189_i2s_priv[i].id];
		const char *property_name =
			mt8189_i2s_priv[i].share_property_name;

		if (of_property_read_string(of_node, property_name, &of_str))
			continue;

		i2s_priv->share_i2s_id = get_i2s_id_by_name(afe, of_str);
	}

	return 0;
}

static int init_i2s_priv_data(struct mtk_base_afe *afe)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv;

	for (int i = 0; i < DAI_I2S_NUM; i++) {
		int id = mt8189_i2s_priv[i].id;
		size_t size = sizeof(struct mtk_afe_i2s_priv);

		if (id >= MT8189_DAI_NUM || id < 0)
			return -EINVAL;

		i2s_priv = devm_kzalloc(afe->dev, size, GFP_KERNEL);
		if (!i2s_priv)
			return -ENOMEM;

		memcpy(i2s_priv, &mt8189_i2s_priv[i], size);

		afe_priv->dai_priv[id] = i2s_priv;
	}

	return 0;
}

int mt8189_dai_i2s_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;
	int ret;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	dai->dai_drivers = mtk_dai_i2s_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_i2s_driver);

	dai->controls = mtk_dai_i2s_controls;
	dai->num_controls = ARRAY_SIZE(mtk_dai_i2s_controls);
	dai->dapm_widgets = mtk_dai_i2s_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_i2s_widgets);
	dai->dapm_routes = mtk_dai_i2s_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_i2s_routes);

	/* set all dai i2s private data */
	ret = init_i2s_priv_data(afe);
	if (ret)
		return ret;

	/* parse share i2s */
	ret = mt8189_dai_i2s_get_share(afe);
	if (ret)
		return ret;

	list_add(&dai->list, &afe->sub_dais);

	return 0;
}
