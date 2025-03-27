// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019, Linaro Limited

#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/wcd934x/registers.h>
#include <linux/mfd/wcd934x/wcd934x.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/slimbus.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include "wcd-clsh-v2.h"
#include "wcd-mbhc-v2.h"

#include <dt-bindings/sound/qcom,wcd934x.h>

#define WCD934X_RATES_MASK (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			    SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			    SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
/* Fractional Rates */
#define WCD934X_FRAC_RATES_MASK (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_88200 |\
				 SNDRV_PCM_RATE_176400)
#define WCD934X_FORMATS_S16_S24_LE (SNDRV_PCM_FMTBIT_S16_LE | \
				    SNDRV_PCM_FMTBIT_S24_LE)

/* slave port water mark level
 *   (0: 6bytes, 1: 9bytes, 2: 12 bytes, 3: 15 bytes)
 */
#define SLAVE_PORT_WATER_MARK_6BYTES	0
#define SLAVE_PORT_WATER_MARK_9BYTES	1
#define SLAVE_PORT_WATER_MARK_12BYTES	2
#define SLAVE_PORT_WATER_MARK_15BYTES	3
#define SLAVE_PORT_WATER_MARK_SHIFT	1
#define SLAVE_PORT_ENABLE		1
#define SLAVE_PORT_DISABLE		0
#define WCD934X_SLIM_WATER_MARK_VAL \
	((SLAVE_PORT_WATER_MARK_12BYTES << SLAVE_PORT_WATER_MARK_SHIFT) | \
	 (SLAVE_PORT_ENABLE))

#define WCD934X_SLIM_NUM_PORT_REG	3
#define WCD934X_SLIM_PGD_PORT_INT_TX_EN0 (WCD934X_SLIM_PGD_PORT_INT_EN0 + 2)
#define WCD934X_SLIM_IRQ_OVERFLOW	BIT(0)
#define WCD934X_SLIM_IRQ_UNDERFLOW	BIT(1)
#define WCD934X_SLIM_IRQ_PORT_CLOSED	BIT(2)

#define WCD934X_MCLK_CLK_12P288MHZ	12288000
#define WCD934X_MCLK_CLK_9P6MHZ		9600000

/* Only valid for 9.6 MHz mclk */
#define WCD9XXX_DMIC_SAMPLE_RATE_2P4MHZ 2400000
#define WCD9XXX_DMIC_SAMPLE_RATE_4P8MHZ 4800000

/* Only valid for 12.288 MHz mclk */
#define WCD9XXX_DMIC_SAMPLE_RATE_4P096MHZ 4096000

#define WCD934X_DMIC_CLK_DIV_2		0x0
#define WCD934X_DMIC_CLK_DIV_3		0x1
#define WCD934X_DMIC_CLK_DIV_4		0x2
#define WCD934X_DMIC_CLK_DIV_6		0x3
#define WCD934X_DMIC_CLK_DIV_8		0x4
#define WCD934X_DMIC_CLK_DIV_16		0x5
#define WCD934X_DMIC_CLK_DRIVE_DEFAULT 0x02

#define TX_HPF_CUT_OFF_FREQ_MASK	0x60
#define CF_MIN_3DB_4HZ			0x0
#define CF_MIN_3DB_75HZ			0x1
#define CF_MIN_3DB_150HZ		0x2

#define WCD934X_RX_START		16
#define WCD934X_NUM_INTERPOLATORS	9
#define WCD934X_RX_PATH_CTL_OFFSET	20
#define WCD934X_MAX_VALID_ADC_MUX	13
#define WCD934X_INVALID_ADC_MUX		9

#define WCD934X_SLIM_RX_CH(p) \
	{.port = p + WCD934X_RX_START, .shift = p,}

#define WCD934X_SLIM_TX_CH(p) \
	{.port = p, .shift = p,}

/* Feature masks to distinguish codec version */
#define DSD_DISABLED_MASK   0
#define SLNQ_DISABLED_MASK  1

#define DSD_DISABLED   BIT(DSD_DISABLED_MASK)
#define SLNQ_DISABLED  BIT(SLNQ_DISABLED_MASK)

/* As fine version info cannot be retrieved before wcd probe.
 * Define three coarse versions for possible future use before wcd probe.
 */
#define WCD_VERSION_WCD9340_1_0     0x400
#define WCD_VERSION_WCD9341_1_0     0x410
#define WCD_VERSION_WCD9340_1_1     0x401
#define WCD_VERSION_WCD9341_1_1     0x411
#define WCD934X_AMIC_PWR_LEVEL_LP	0
#define WCD934X_AMIC_PWR_LEVEL_DEFAULT	1
#define WCD934X_AMIC_PWR_LEVEL_HP	2
#define WCD934X_AMIC_PWR_LEVEL_HYBRID	3
#define WCD934X_AMIC_PWR_LVL_MASK	0x60
#define WCD934X_AMIC_PWR_LVL_SHIFT	0x5

#define WCD934X_DEC_PWR_LVL_MASK	0x06
#define WCD934X_DEC_PWR_LVL_LP		0x02
#define WCD934X_DEC_PWR_LVL_HP		0x04
#define WCD934X_DEC_PWR_LVL_DF		0x00
#define WCD934X_DEC_PWR_LVL_HYBRID WCD934X_DEC_PWR_LVL_DF

#define WCD934X_DEF_MICBIAS_MV	1800
#define WCD934X_MAX_MICBIAS_MV	2850

#define WCD_IIR_FILTER_SIZE	(sizeof(u32) * BAND_MAX)

#define WCD_IIR_FILTER_CTL(xname, iidx, bidx) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = wcd934x_iir_filter_info, \
	.get = wcd934x_get_iir_band_audio_mixer, \
	.put = wcd934x_put_iir_band_audio_mixer, \
	.private_value = (unsigned long)&(struct wcd_iir_filter_ctl) { \
		.iir_idx = iidx, \
		.band_idx = bidx, \
		.bytes_ext = {.max = WCD_IIR_FILTER_SIZE, }, \
	} \
}

/* Z value defined in milliohm */
#define WCD934X_ZDET_VAL_32             32000
#define WCD934X_ZDET_VAL_400            400000
#define WCD934X_ZDET_VAL_1200           1200000
#define WCD934X_ZDET_VAL_100K           100000000
/* Z floating defined in ohms */
#define WCD934X_ZDET_FLOATING_IMPEDANCE 0x0FFFFFFE

#define WCD934X_ZDET_NUM_MEASUREMENTS   900
#define WCD934X_MBHC_GET_C1(c)          ((c & 0xC000) >> 14)
#define WCD934X_MBHC_GET_X1(x)          (x & 0x3FFF)
/* Z value compared in milliOhm */
#define WCD934X_MBHC_IS_SECOND_RAMP_REQUIRED(z) ((z > 400000) || (z < 32000))
#define WCD934X_MBHC_ZDET_CONST         (86 * 16384)
#define WCD934X_MBHC_MOISTURE_RREF      R_24_KOHM
#define WCD934X_MBHC_MAX_BUTTONS	(8)
#define WCD_MBHC_HS_V_MAX           1600

#define WCD934X_INTERPOLATOR_PATH(id)			\
	{"RX INT" #id "_1 MIX1 INP0", "RX0", "SLIM RX0"},	\
	{"RX INT" #id "_1 MIX1 INP0", "RX1", "SLIM RX1"},	\
	{"RX INT" #id "_1 MIX1 INP0", "RX2", "SLIM RX2"},	\
	{"RX INT" #id "_1 MIX1 INP0", "RX3", "SLIM RX3"},	\
	{"RX INT" #id "_1 MIX1 INP0", "RX4", "SLIM RX4"},	\
	{"RX INT" #id "_1 MIX1 INP0", "RX5", "SLIM RX5"},	\
	{"RX INT" #id "_1 MIX1 INP0", "RX6", "SLIM RX6"},	\
	{"RX INT" #id "_1 MIX1 INP0", "RX7", "SLIM RX7"},	\
	{"RX INT" #id "_1 MIX1 INP0", "IIR0", "IIR0"},	\
	{"RX INT" #id "_1 MIX1 INP0", "IIR1", "IIR1"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX0", "SLIM RX0"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX1", "SLIM RX1"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX2", "SLIM RX2"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX3", "SLIM RX3"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX4", "SLIM RX4"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX5", "SLIM RX5"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX6", "SLIM RX6"},	\
	{"RX INT" #id "_1 MIX1 INP1", "RX7", "SLIM RX7"},	\
	{"RX INT" #id "_1 MIX1 INP1", "IIR0", "IIR0"},	\
	{"RX INT" #id "_1 MIX1 INP1", "IIR1", "IIR1"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX0", "SLIM RX0"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX1", "SLIM RX1"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX2", "SLIM RX2"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX3", "SLIM RX3"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX4", "SLIM RX4"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX5", "SLIM RX5"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX6", "SLIM RX6"},	\
	{"RX INT" #id "_1 MIX1 INP2", "RX7", "SLIM RX7"},	\
	{"RX INT" #id "_1 MIX1 INP2", "IIR0", "IIR0"},		\
	{"RX INT" #id "_1 MIX1 INP2", "IIR1", "IIR1"},		\
	{"RX INT" #id "_1 MIX1", NULL, "RX INT" #id "_1 MIX1 INP0"}, \
	{"RX INT" #id "_1 MIX1", NULL, "RX INT" #id "_1 MIX1 INP1"}, \
	{"RX INT" #id "_1 MIX1", NULL, "RX INT" #id "_1 MIX1 INP2"}, \
	{"RX INT" #id "_2 MUX", "RX0", "SLIM RX0"},	\
	{"RX INT" #id "_2 MUX", "RX1", "SLIM RX1"},	\
	{"RX INT" #id "_2 MUX", "RX2", "SLIM RX2"},	\
	{"RX INT" #id "_2 MUX", "RX3", "SLIM RX3"},	\
	{"RX INT" #id "_2 MUX", "RX4", "SLIM RX4"},	\
	{"RX INT" #id "_2 MUX", "RX5", "SLIM RX5"},	\
	{"RX INT" #id "_2 MUX", "RX6", "SLIM RX6"},	\
	{"RX INT" #id "_2 MUX", "RX7", "SLIM RX7"},	\
	{"RX INT" #id "_2 MUX", NULL, "INT" #id "_CLK"}, \
	{"RX INT" #id "_2 MUX", NULL, "DSMDEM" #id "_CLK"}, \
	{"RX INT" #id "_2 INTERP", NULL, "RX INT" #id "_2 MUX"},	\
	{"RX INT" #id " SEC MIX", NULL, "RX INT" #id "_2 INTERP"},	\
	{"RX INT" #id "_1 INTERP", NULL, "RX INT" #id "_1 MIX1"},	\
	{"RX INT" #id "_1 INTERP", NULL, "INT" #id "_CLK"},	\
	{"RX INT" #id "_1 INTERP", NULL, "DSMDEM" #id "_CLK"},	\
	{"RX INT" #id " SEC MIX", NULL, "RX INT" #id "_1 INTERP"}

#define WCD934X_INTERPOLATOR_MIX2(id)			\
	{"RX INT" #id " MIX2", NULL, "RX INT" #id " SEC MIX"}, \
	{"RX INT" #id " MIX2", NULL, "RX INT" #id " MIX2 INP"}

#define WCD934X_SLIM_RX_AIF_PATH(id)	\
	{"SLIM RX"#id" MUX", "AIF1_PB", "AIF1 PB"},	\
	{"SLIM RX"#id" MUX", "AIF2_PB", "AIF2 PB"},	\
	{"SLIM RX"#id" MUX", "AIF3_PB", "AIF3 PB"},	\
	{"SLIM RX"#id" MUX", "AIF4_PB", "AIF4 PB"},   \
	{"SLIM RX"#id, NULL, "SLIM RX"#id" MUX"}

#define WCD934X_ADC_MUX(id) \
	{"ADC MUX" #id, "DMIC", "DMIC MUX" #id },	\
	{"ADC MUX" #id, "AMIC", "AMIC MUX" #id },	\
	{"DMIC MUX" #id, "DMIC0", "DMIC0"},		\
	{"DMIC MUX" #id, "DMIC1", "DMIC1"},		\
	{"DMIC MUX" #id, "DMIC2", "DMIC2"},		\
	{"DMIC MUX" #id, "DMIC3", "DMIC3"},		\
	{"DMIC MUX" #id, "DMIC4", "DMIC4"},		\
	{"DMIC MUX" #id, "DMIC5", "DMIC5"},		\
	{"AMIC MUX" #id, "ADC1", "ADC1"},		\
	{"AMIC MUX" #id, "ADC2", "ADC2"},		\
	{"AMIC MUX" #id, "ADC3", "ADC3"},		\
	{"AMIC MUX" #id, "ADC4", "ADC4"}

#define WCD934X_IIR_INP_MUX(id) \
	{"IIR" #id, NULL, "IIR" #id " INP0 MUX"},	\
	{"IIR" #id " INP0 MUX", "DEC0", "ADC MUX0"},	\
	{"IIR" #id " INP0 MUX", "DEC1", "ADC MUX1"},	\
	{"IIR" #id " INP0 MUX", "DEC2", "ADC MUX2"},	\
	{"IIR" #id " INP0 MUX", "DEC3", "ADC MUX3"},	\
	{"IIR" #id " INP0 MUX", "DEC4", "ADC MUX4"},	\
	{"IIR" #id " INP0 MUX", "DEC5", "ADC MUX5"},	\
	{"IIR" #id " INP0 MUX", "DEC6", "ADC MUX6"},	\
	{"IIR" #id " INP0 MUX", "DEC7", "ADC MUX7"},	\
	{"IIR" #id " INP0 MUX", "DEC8", "ADC MUX8"},	\
	{"IIR" #id " INP0 MUX", "RX0", "SLIM RX0"},	\
	{"IIR" #id " INP0 MUX", "RX1", "SLIM RX1"},	\
	{"IIR" #id " INP0 MUX", "RX2", "SLIM RX2"},	\
	{"IIR" #id " INP0 MUX", "RX3", "SLIM RX3"},	\
	{"IIR" #id " INP0 MUX", "RX4", "SLIM RX4"},	\
	{"IIR" #id " INP0 MUX", "RX5", "SLIM RX5"},	\
	{"IIR" #id " INP0 MUX", "RX6", "SLIM RX6"},	\
	{"IIR" #id " INP0 MUX", "RX7", "SLIM RX7"},	\
	{"IIR" #id, NULL, "IIR" #id " INP1 MUX"},	\
	{"IIR" #id " INP1 MUX", "DEC0", "ADC MUX0"},	\
	{"IIR" #id " INP1 MUX", "DEC1", "ADC MUX1"},	\
	{"IIR" #id " INP1 MUX", "DEC2", "ADC MUX2"},	\
	{"IIR" #id " INP1 MUX", "DEC3", "ADC MUX3"},	\
	{"IIR" #id " INP1 MUX", "DEC4", "ADC MUX4"},	\
	{"IIR" #id " INP1 MUX", "DEC5", "ADC MUX5"},	\
	{"IIR" #id " INP1 MUX", "DEC6", "ADC MUX6"},	\
	{"IIR" #id " INP1 MUX", "DEC7", "ADC MUX7"},	\
	{"IIR" #id " INP1 MUX", "DEC8", "ADC MUX8"},	\
	{"IIR" #id " INP1 MUX", "RX0", "SLIM RX0"},	\
	{"IIR" #id " INP1 MUX", "RX1", "SLIM RX1"},	\
	{"IIR" #id " INP1 MUX", "RX2", "SLIM RX2"},	\
	{"IIR" #id " INP1 MUX", "RX3", "SLIM RX3"},	\
	{"IIR" #id " INP1 MUX", "RX4", "SLIM RX4"},	\
	{"IIR" #id " INP1 MUX", "RX5", "SLIM RX5"},	\
	{"IIR" #id " INP1 MUX", "RX6", "SLIM RX6"},	\
	{"IIR" #id " INP1 MUX", "RX7", "SLIM RX7"},	\
	{"IIR" #id, NULL, "IIR" #id " INP2 MUX"},	\
	{"IIR" #id " INP2 MUX", "DEC0", "ADC MUX0"},	\
	{"IIR" #id " INP2 MUX", "DEC1", "ADC MUX1"},	\
	{"IIR" #id " INP2 MUX", "DEC2", "ADC MUX2"},	\
	{"IIR" #id " INP2 MUX", "DEC3", "ADC MUX3"},	\
	{"IIR" #id " INP2 MUX", "DEC4", "ADC MUX4"},	\
	{"IIR" #id " INP2 MUX", "DEC5", "ADC MUX5"},	\
	{"IIR" #id " INP2 MUX", "DEC6", "ADC MUX6"},	\
	{"IIR" #id " INP2 MUX", "DEC7", "ADC MUX7"},	\
	{"IIR" #id " INP2 MUX", "DEC8", "ADC MUX8"},	\
	{"IIR" #id " INP2 MUX", "RX0", "SLIM RX0"},	\
	{"IIR" #id " INP2 MUX", "RX1", "SLIM RX1"},	\
	{"IIR" #id " INP2 MUX", "RX2", "SLIM RX2"},	\
	{"IIR" #id " INP2 MUX", "RX3", "SLIM RX3"},	\
	{"IIR" #id " INP2 MUX", "RX4", "SLIM RX4"},	\
	{"IIR" #id " INP2 MUX", "RX5", "SLIM RX5"},	\
	{"IIR" #id " INP2 MUX", "RX6", "SLIM RX6"},	\
	{"IIR" #id " INP2 MUX", "RX7", "SLIM RX7"},	\
	{"IIR" #id, NULL, "IIR" #id " INP3 MUX"},	\
	{"IIR" #id " INP3 MUX", "DEC0", "ADC MUX0"},	\
	{"IIR" #id " INP3 MUX", "DEC1", "ADC MUX1"},	\
	{"IIR" #id " INP3 MUX", "DEC2", "ADC MUX2"},	\
	{"IIR" #id " INP3 MUX", "DEC3", "ADC MUX3"},	\
	{"IIR" #id " INP3 MUX", "DEC4", "ADC MUX4"},	\
	{"IIR" #id " INP3 MUX", "DEC5", "ADC MUX5"},	\
	{"IIR" #id " INP3 MUX", "DEC6", "ADC MUX6"},	\
	{"IIR" #id " INP3 MUX", "DEC7", "ADC MUX7"},	\
	{"IIR" #id " INP3 MUX", "DEC8", "ADC MUX8"},	\
	{"IIR" #id " INP3 MUX", "RX0", "SLIM RX0"},	\
	{"IIR" #id " INP3 MUX", "RX1", "SLIM RX1"},	\
	{"IIR" #id " INP3 MUX", "RX2", "SLIM RX2"},	\
	{"IIR" #id " INP3 MUX", "RX3", "SLIM RX3"},	\
	{"IIR" #id " INP3 MUX", "RX4", "SLIM RX4"},	\
	{"IIR" #id " INP3 MUX", "RX5", "SLIM RX5"},	\
	{"IIR" #id " INP3 MUX", "RX6", "SLIM RX6"},	\
	{"IIR" #id " INP3 MUX", "RX7", "SLIM RX7"}

#define WCD934X_SLIM_TX_AIF_PATH(id)	\
	{"AIF1_CAP Mixer", "SLIM TX" #id, "SLIM TX" #id },	\
	{"AIF2_CAP Mixer", "SLIM TX" #id, "SLIM TX" #id },	\
	{"AIF3_CAP Mixer", "SLIM TX" #id, "SLIM TX" #id },	\
	{"SLIM TX" #id, NULL, "CDC_IF TX" #id " MUX"}

#define WCD934X_MAX_MICBIAS	MIC_BIAS_4
#define NUM_CODEC_DAIS          9

enum {
	SIDO_SOURCE_INTERNAL,
	SIDO_SOURCE_RCO_BG,
};

enum {
	INTERP_EAR = 0,
	INTERP_HPHL,
	INTERP_HPHR,
	INTERP_LO1,
	INTERP_LO2,
	INTERP_LO3_NA, /* LO3 not avalible in Tavil */
	INTERP_LO4_NA,
	INTERP_SPKR1, /*INT7 WSA Speakers via soundwire */
	INTERP_SPKR2, /*INT8 WSA Speakers via soundwire */
	INTERP_MAX,
};

enum {
	WCD934X_RX0 = 0,
	WCD934X_RX1,
	WCD934X_RX2,
	WCD934X_RX3,
	WCD934X_RX4,
	WCD934X_RX5,
	WCD934X_RX6,
	WCD934X_RX7,
	WCD934X_RX8,
	WCD934X_RX9,
	WCD934X_RX10,
	WCD934X_RX11,
	WCD934X_RX12,
	WCD934X_RX_MAX,
};

enum {
	WCD934X_TX0 = 0,
	WCD934X_TX1,
	WCD934X_TX2,
	WCD934X_TX3,
	WCD934X_TX4,
	WCD934X_TX5,
	WCD934X_TX6,
	WCD934X_TX7,
	WCD934X_TX8,
	WCD934X_TX9,
	WCD934X_TX10,
	WCD934X_TX11,
	WCD934X_TX12,
	WCD934X_TX13,
	WCD934X_TX14,
	WCD934X_TX15,
	WCD934X_TX_MAX,
};

struct wcd934x_slim_ch {
	u32 ch_num;
	u16 port;
	u16 shift;
	struct list_head list;
};

static const struct wcd934x_slim_ch wcd934x_tx_chs[WCD934X_TX_MAX] = {
	WCD934X_SLIM_TX_CH(0),
	WCD934X_SLIM_TX_CH(1),
	WCD934X_SLIM_TX_CH(2),
	WCD934X_SLIM_TX_CH(3),
	WCD934X_SLIM_TX_CH(4),
	WCD934X_SLIM_TX_CH(5),
	WCD934X_SLIM_TX_CH(6),
	WCD934X_SLIM_TX_CH(7),
	WCD934X_SLIM_TX_CH(8),
	WCD934X_SLIM_TX_CH(9),
	WCD934X_SLIM_TX_CH(10),
	WCD934X_SLIM_TX_CH(11),
	WCD934X_SLIM_TX_CH(12),
	WCD934X_SLIM_TX_CH(13),
	WCD934X_SLIM_TX_CH(14),
	WCD934X_SLIM_TX_CH(15),
};

static const struct wcd934x_slim_ch wcd934x_rx_chs[WCD934X_RX_MAX] = {
	WCD934X_SLIM_RX_CH(0),	 /* 16 */
	WCD934X_SLIM_RX_CH(1),	 /* 17 */
	WCD934X_SLIM_RX_CH(2),
	WCD934X_SLIM_RX_CH(3),
	WCD934X_SLIM_RX_CH(4),
	WCD934X_SLIM_RX_CH(5),
	WCD934X_SLIM_RX_CH(6),
	WCD934X_SLIM_RX_CH(7),
	WCD934X_SLIM_RX_CH(8),
	WCD934X_SLIM_RX_CH(9),
	WCD934X_SLIM_RX_CH(10),
	WCD934X_SLIM_RX_CH(11),
	WCD934X_SLIM_RX_CH(12),
};

/* Codec supports 2 IIR filters */
enum {
	IIR0 = 0,
	IIR1,
	IIR_MAX,
};

/* Each IIR has 5 Filter Stages */
enum {
	BAND1 = 0,
	BAND2,
	BAND3,
	BAND4,
	BAND5,
	BAND_MAX,
};

enum {
	COMPANDER_1, /* HPH_L */
	COMPANDER_2, /* HPH_R */
	COMPANDER_3, /* LO1_DIFF */
	COMPANDER_4, /* LO2_DIFF */
	COMPANDER_5, /* LO3_SE - not used in Tavil */
	COMPANDER_6, /* LO4_SE - not used in Tavil */
	COMPANDER_7, /* SWR SPK CH1 */
	COMPANDER_8, /* SWR SPK CH2 */
	COMPANDER_MAX,
};

enum {
	INTn_1_INP_SEL_ZERO = 0,
	INTn_1_INP_SEL_DEC0,
	INTn_1_INP_SEL_DEC1,
	INTn_1_INP_SEL_IIR0,
	INTn_1_INP_SEL_IIR1,
	INTn_1_INP_SEL_RX0,
	INTn_1_INP_SEL_RX1,
	INTn_1_INP_SEL_RX2,
	INTn_1_INP_SEL_RX3,
	INTn_1_INP_SEL_RX4,
	INTn_1_INP_SEL_RX5,
	INTn_1_INP_SEL_RX6,
	INTn_1_INP_SEL_RX7,
};

enum {
	INTn_2_INP_SEL_ZERO = 0,
	INTn_2_INP_SEL_RX0,
	INTn_2_INP_SEL_RX1,
	INTn_2_INP_SEL_RX2,
	INTn_2_INP_SEL_RX3,
	INTn_2_INP_SEL_RX4,
	INTn_2_INP_SEL_RX5,
	INTn_2_INP_SEL_RX6,
	INTn_2_INP_SEL_RX7,
	INTn_2_INP_SEL_PROXIMITY,
};

struct interp_sample_rate {
	int sample_rate;
	int rate_val;
};

static const struct interp_sample_rate sr_val_tbl[] = {
	{8000, 0x0},
	{16000, 0x1},
	{32000, 0x3},
	{48000, 0x4},
	{96000, 0x5},
	{192000, 0x6},
	{384000, 0x7},
	{44100, 0x9},
	{88200, 0xA},
	{176400, 0xB},
	{352800, 0xC},
};

struct wcd934x_mbhc_zdet_param {
	u16 ldo_ctl;
	u16 noff;
	u16 nshift;
	u16 btn5;
	u16 btn6;
	u16 btn7;
};

struct wcd_slim_codec_dai_data {
	struct list_head slim_ch_list;
	struct slim_stream_config sconfig;
	struct slim_stream_runtime *sruntime;
};

static const struct regmap_range_cfg wcd934x_ifc_ranges[] = {
	{
		.name = "WCD9335-IFC-DEV",
		.range_min =  0x0,
		.range_max = 0xffff,
		.selector_reg = 0x800,
		.selector_mask = 0xfff,
		.selector_shift = 0,
		.window_start = 0x800,
		.window_len = 0x400,
	},
};

static const struct regmap_config wcd934x_ifc_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.ranges = wcd934x_ifc_ranges,
	.num_ranges = ARRAY_SIZE(wcd934x_ifc_ranges),
};

struct wcd934x_codec {
	struct device *dev;
	struct clk_hw hw;
	struct clk *extclk;
	struct regmap *regmap;
	struct regmap *if_regmap;
	struct slim_device *sdev;
	struct slim_device *sidev;
	struct wcd_clsh_ctrl *clsh_ctrl;
	struct snd_soc_component *component;
	struct wcd934x_slim_ch rx_chs[WCD934X_RX_MAX];
	struct wcd934x_slim_ch tx_chs[WCD934X_TX_MAX];
	struct wcd_slim_codec_dai_data dai[NUM_CODEC_DAIS];
	int rate;
	u32 version;
	u32 hph_mode;
	int num_rx_port;
	int num_tx_port;
	u32 tx_port_value[WCD934X_TX_MAX];
	u32 rx_port_value[WCD934X_RX_MAX];
	int sido_input_src;
	int dmic_0_1_clk_cnt;
	int dmic_2_3_clk_cnt;
	int dmic_4_5_clk_cnt;
	int dmic_sample_rate;
	int comp_enabled[COMPANDER_MAX];
	int sysclk_users;
	struct mutex sysclk_mutex;
	/* mbhc module */
	struct wcd_mbhc *mbhc;
	struct wcd_mbhc_config mbhc_cfg;
	struct wcd_mbhc_intr intr_ids;
	bool mbhc_started;
	struct mutex micb_lock;
	u32 micb_ref[WCD934X_MAX_MICBIAS];
	u32 pullup_ref[WCD934X_MAX_MICBIAS];
	u32 micb2_mv;
};

#define to_wcd934x_codec(_hw) container_of(_hw, struct wcd934x_codec, hw)

struct wcd_iir_filter_ctl {
	unsigned int iir_idx;
	unsigned int band_idx;
	struct soc_bytes_ext bytes_ext;
};

static const DECLARE_TLV_DB_SCALE(digital_gain, -8400, 100, -8400);
static const DECLARE_TLV_DB_SCALE(line_gain, 0, 7, 1);
static const DECLARE_TLV_DB_SCALE(analog_gain, 0, 25, 1);
static const DECLARE_TLV_DB_SCALE(ear_pa_gain, 0, 150, 0);

/* Cutoff frequency for high pass filter */
static const char * const cf_text[] = {
	"CF_NEG_3DB_4HZ", "CF_NEG_3DB_75HZ", "CF_NEG_3DB_150HZ"
};

static const char * const rx_cf_text[] = {
	"CF_NEG_3DB_4HZ", "CF_NEG_3DB_75HZ", "CF_NEG_3DB_150HZ",
	"CF_NEG_3DB_0P48HZ"
};

static const char * const rx_hph_mode_mux_text[] = {
	"Class H Invalid", "Class-H Hi-Fi", "Class-H Low Power", "Class-AB",
	"Class-H Hi-Fi Low Power"
};

static const char *const slim_rx_mux_text[] = {
	"ZERO", "AIF1_PB", "AIF2_PB", "AIF3_PB", "AIF4_PB",
};

static const char * const rx_int0_7_mix_mux_text[] = {
	"ZERO", "RX0", "RX1", "RX2", "RX3", "RX4", "RX5",
	"RX6", "RX7", "PROXIMITY"
};

static const char * const rx_int_mix_mux_text[] = {
	"ZERO", "RX0", "RX1", "RX2", "RX3", "RX4", "RX5",
	"RX6", "RX7"
};

static const char * const rx_prim_mix_text[] = {
	"ZERO", "DEC0", "DEC1", "IIR0", "IIR1", "RX0", "RX1", "RX2",
	"RX3", "RX4", "RX5", "RX6", "RX7"
};

static const char * const rx_sidetone_mix_text[] = {
	"ZERO", "SRC0", "SRC1", "SRC_SUM"
};

static const char * const iir_inp_mux_text[] = {
	"ZERO", "DEC0", "DEC1", "DEC2", "DEC3", "DEC4", "DEC5", "DEC6",
	"DEC7", "DEC8", "RX0", "RX1", "RX2", "RX3", "RX4", "RX5", "RX6", "RX7"
};

static const char * const rx_int_dem_inp_mux_text[] = {
	"NORMAL_DSM_OUT", "CLSH_DSM_OUT",
};

static const char * const rx_int0_1_interp_mux_text[] = {
	"ZERO", "RX INT0_1 MIX1",
};

static const char * const rx_int1_1_interp_mux_text[] = {
	"ZERO", "RX INT1_1 MIX1",
};

static const char * const rx_int2_1_interp_mux_text[] = {
	"ZERO", "RX INT2_1 MIX1",
};

static const char * const rx_int3_1_interp_mux_text[] = {
	"ZERO", "RX INT3_1 MIX1",
};

static const char * const rx_int4_1_interp_mux_text[] = {
	"ZERO", "RX INT4_1 MIX1",
};

static const char * const rx_int7_1_interp_mux_text[] = {
	"ZERO", "RX INT7_1 MIX1",
};

static const char * const rx_int8_1_interp_mux_text[] = {
	"ZERO", "RX INT8_1 MIX1",
};

static const char * const rx_int0_2_interp_mux_text[] = {
	"ZERO", "RX INT0_2 MUX",
};

static const char * const rx_int1_2_interp_mux_text[] = {
	"ZERO", "RX INT1_2 MUX",
};

static const char * const rx_int2_2_interp_mux_text[] = {
	"ZERO", "RX INT2_2 MUX",
};

static const char * const rx_int3_2_interp_mux_text[] = {
	"ZERO", "RX INT3_2 MUX",
};

static const char * const rx_int4_2_interp_mux_text[] = {
	"ZERO", "RX INT4_2 MUX",
};

static const char * const rx_int7_2_interp_mux_text[] = {
	"ZERO", "RX INT7_2 MUX",
};

static const char * const rx_int8_2_interp_mux_text[] = {
	"ZERO", "RX INT8_2 MUX",
};

static const char * const dmic_mux_text[] = {
	"ZERO", "DMIC0", "DMIC1", "DMIC2", "DMIC3", "DMIC4", "DMIC5"
};

static const char * const amic_mux_text[] = {
	"ZERO", "ADC1", "ADC2", "ADC3", "ADC4"
};

static const char * const amic4_5_sel_text[] = {
	"AMIC4", "AMIC5"
};

static const char * const adc_mux_text[] = {
	"DMIC", "AMIC", "ANC_FB_TUNE1", "ANC_FB_TUNE2"
};

static const char * const cdc_if_tx0_mux_text[] = {
	"ZERO", "RX_MIX_TX0", "DEC0", "DEC0_192"
};

static const char * const cdc_if_tx1_mux_text[] = {
	"ZERO", "RX_MIX_TX1", "DEC1", "DEC1_192"
};

static const char * const cdc_if_tx2_mux_text[] = {
	"ZERO", "RX_MIX_TX2", "DEC2", "DEC2_192"
};

static const char * const cdc_if_tx3_mux_text[] = {
	"ZERO", "RX_MIX_TX3", "DEC3", "DEC3_192"
};

static const char * const cdc_if_tx4_mux_text[] = {
	"ZERO", "RX_MIX_TX4", "DEC4", "DEC4_192"
};

static const char * const cdc_if_tx5_mux_text[] = {
	"ZERO", "RX_MIX_TX5", "DEC5", "DEC5_192"
};

static const char * const cdc_if_tx6_mux_text[] = {
	"ZERO", "RX_MIX_TX6", "DEC6", "DEC6_192"
};

static const char * const cdc_if_tx7_mux_text[] = {
	"ZERO", "RX_MIX_TX7", "DEC7", "DEC7_192"
};

static const char * const cdc_if_tx8_mux_text[] = {
	"ZERO", "RX_MIX_TX8", "DEC8", "DEC8_192"
};

static const char * const cdc_if_tx9_mux_text[] = {
	"ZERO", "DEC7", "DEC7_192"
};

static const char * const cdc_if_tx10_mux_text[] = {
	"ZERO", "DEC6", "DEC6_192"
};

static const char * const cdc_if_tx11_mux_text[] = {
	"DEC_0_5", "DEC_9_12", "MAD_AUDIO", "MAD_BRDCST"
};

static const char * const cdc_if_tx11_inp1_mux_text[] = {
	"ZERO", "DEC0", "DEC1", "DEC2", "DEC3", "DEC4",
	"DEC5", "RX_MIX_TX5", "DEC9_10", "DEC11_12"
};

static const char * const cdc_if_tx13_mux_text[] = {
	"CDC_DEC_5", "MAD_BRDCST"
};

static const char * const cdc_if_tx13_inp1_mux_text[] = {
	"ZERO", "DEC5", "DEC5_192"
};

static const struct soc_enum cf_dec0_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX0_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX1_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec2_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX2_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec3_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX3_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec4_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX4_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec5_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX5_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec6_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX6_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec7_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX7_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec8_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX8_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_int0_1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX0_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int0_2_enum, WCD934X_CDC_RX0_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int1_1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX1_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int1_2_enum, WCD934X_CDC_RX1_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int2_1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX2_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int2_2_enum, WCD934X_CDC_RX2_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int3_1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX3_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int3_2_enum, WCD934X_CDC_RX3_RX_PATH_MIX_CFG, 2,
			    rx_cf_text);

static const struct soc_enum cf_int4_1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX4_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int4_2_enum, WCD934X_CDC_RX4_RX_PATH_MIX_CFG, 2,
			    rx_cf_text);

static const struct soc_enum cf_int7_1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX7_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int7_2_enum, WCD934X_CDC_RX7_RX_PATH_MIX_CFG, 2,
			    rx_cf_text);

static const struct soc_enum cf_int8_1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX8_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int8_2_enum, WCD934X_CDC_RX8_RX_PATH_MIX_CFG, 2,
			    rx_cf_text);

static const struct soc_enum rx_hph_mode_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_hph_mode_mux_text),
			    rx_hph_mode_mux_text);

static const struct soc_enum slim_rx_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(slim_rx_mux_text), slim_rx_mux_text);

static const struct soc_enum rx_int0_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT0_CFG1, 0, 10,
			rx_int0_7_mix_mux_text);

static const struct soc_enum rx_int1_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT1_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int2_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT2_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int3_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT3_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int4_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT4_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int7_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT7_CFG1, 0, 10,
			rx_int0_7_mix_mux_text);

static const struct soc_enum rx_int8_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT8_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int0_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT0_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int0_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT0_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int0_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT0_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int1_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT1_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int1_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT1_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int1_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT1_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int2_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT2_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int2_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT2_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int2_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT2_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int3_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT3_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int3_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT3_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int3_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT3_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int4_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT4_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int4_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT4_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int4_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT4_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int7_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT7_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int7_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT7_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int7_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT7_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int8_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT8_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int8_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT8_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int8_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_RX_INT8_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int0_mix2_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 0, 4,
			rx_sidetone_mix_text);

static const struct soc_enum rx_int1_mix2_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 2, 4,
			rx_sidetone_mix_text);

static const struct soc_enum rx_int2_mix2_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 4, 4,
			rx_sidetone_mix_text);

static const struct soc_enum rx_int3_mix2_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 6, 4,
			rx_sidetone_mix_text);

static const struct soc_enum rx_int4_mix2_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_SIDETONE_SRC_CFG1, 0, 4,
			rx_sidetone_mix_text);

static const struct soc_enum rx_int7_mix2_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX_INP_MUX_SIDETONE_SRC_CFG1, 2, 4,
			rx_sidetone_mix_text);

static const struct soc_enum iir0_inp0_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR0_MIX_CFG0,
			0, 18, iir_inp_mux_text);

static const struct soc_enum iir0_inp1_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR0_MIX_CFG1,
			0, 18, iir_inp_mux_text);

static const struct soc_enum iir0_inp2_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR0_MIX_CFG2,
			0, 18, iir_inp_mux_text);

static const struct soc_enum iir0_inp3_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR0_MIX_CFG3,
			0, 18, iir_inp_mux_text);

static const struct soc_enum iir1_inp0_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR1_MIX_CFG0,
			0, 18, iir_inp_mux_text);

static const struct soc_enum iir1_inp1_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR1_MIX_CFG1,
			0, 18, iir_inp_mux_text);

static const struct soc_enum iir1_inp2_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR1_MIX_CFG2,
			0, 18, iir_inp_mux_text);

static const struct soc_enum iir1_inp3_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_SIDETONE_IIR_INP_MUX_IIR1_MIX_CFG3,
			0, 18, iir_inp_mux_text);

static const struct soc_enum rx_int0_dem_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX0_RX_PATH_SEC0, 0,
			ARRAY_SIZE(rx_int_dem_inp_mux_text),
			rx_int_dem_inp_mux_text);

static const struct soc_enum rx_int1_dem_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX1_RX_PATH_SEC0, 0,
			ARRAY_SIZE(rx_int_dem_inp_mux_text),
			rx_int_dem_inp_mux_text);

static const struct soc_enum rx_int2_dem_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_RX2_RX_PATH_SEC0, 0,
			ARRAY_SIZE(rx_int_dem_inp_mux_text),
			rx_int_dem_inp_mux_text);

static const struct soc_enum tx_adc_mux0_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG1, 0,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX1_CFG1, 0,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux2_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX2_CFG1, 0,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux3_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG1, 0,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux4_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG1, 2,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux5_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX1_CFG1, 2,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux6_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX2_CFG1, 2,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux7_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG1, 2,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);
static const struct soc_enum tx_adc_mux8_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX1_CFG1, 4,
			ARRAY_SIZE(adc_mux_text), adc_mux_text);

static const struct soc_enum rx_int0_1_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2,
			rx_int0_1_interp_mux_text);

static const struct soc_enum rx_int1_1_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2,
			rx_int1_1_interp_mux_text);

static const struct soc_enum rx_int2_1_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2,
			rx_int2_1_interp_mux_text);

static const struct soc_enum rx_int3_1_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int3_1_interp_mux_text);

static const struct soc_enum rx_int4_1_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int4_1_interp_mux_text);

static const struct soc_enum rx_int7_1_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int7_1_interp_mux_text);

static const struct soc_enum rx_int8_1_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int8_1_interp_mux_text);

static const struct soc_enum rx_int0_2_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int0_2_interp_mux_text);

static const struct soc_enum rx_int1_2_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int1_2_interp_mux_text);

static const struct soc_enum rx_int2_2_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int2_2_interp_mux_text);

static const struct soc_enum rx_int3_2_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int3_2_interp_mux_text);

static const struct soc_enum rx_int4_2_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int4_2_interp_mux_text);

static const struct soc_enum rx_int7_2_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int7_2_interp_mux_text);

static const struct soc_enum rx_int8_2_interp_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM,	0, 2, rx_int8_2_interp_mux_text);

static const struct soc_enum tx_dmic_mux0_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX1_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux2_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX2_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux3_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux4_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux5_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX5_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux6_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX6_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux7_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX7_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_dmic_mux8_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX8_CFG0, 3, 7,
			dmic_mux_text);

static const struct soc_enum tx_amic_mux0_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux1_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX1_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux2_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX2_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux3_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux4_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux5_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX5_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux6_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX6_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux7_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX7_CFG0, 0, 5,
			amic_mux_text);
static const struct soc_enum tx_amic_mux8_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_TX_INP_MUX_ADC_MUX8_CFG0, 0, 5,
			amic_mux_text);

static const struct soc_enum tx_amic4_5_enum =
	SOC_ENUM_SINGLE(WCD934X_TX_NEW_AMIC_4_5_SEL, 7, 2, amic4_5_sel_text);

static const struct soc_enum cdc_if_tx0_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG0, 0,
			ARRAY_SIZE(cdc_if_tx0_mux_text), cdc_if_tx0_mux_text);
static const struct soc_enum cdc_if_tx1_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG0, 2,
			ARRAY_SIZE(cdc_if_tx1_mux_text), cdc_if_tx1_mux_text);
static const struct soc_enum cdc_if_tx2_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG0, 4,
			ARRAY_SIZE(cdc_if_tx2_mux_text), cdc_if_tx2_mux_text);
static const struct soc_enum cdc_if_tx3_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG0, 6,
			ARRAY_SIZE(cdc_if_tx3_mux_text), cdc_if_tx3_mux_text);
static const struct soc_enum cdc_if_tx4_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG1, 0,
			ARRAY_SIZE(cdc_if_tx4_mux_text), cdc_if_tx4_mux_text);
static const struct soc_enum cdc_if_tx5_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG1, 2,
			ARRAY_SIZE(cdc_if_tx5_mux_text), cdc_if_tx5_mux_text);
static const struct soc_enum cdc_if_tx6_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG1, 4,
			ARRAY_SIZE(cdc_if_tx6_mux_text), cdc_if_tx6_mux_text);
static const struct soc_enum cdc_if_tx7_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG1, 6,
			ARRAY_SIZE(cdc_if_tx7_mux_text), cdc_if_tx7_mux_text);
static const struct soc_enum cdc_if_tx8_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG2, 0,
			ARRAY_SIZE(cdc_if_tx8_mux_text), cdc_if_tx8_mux_text);
static const struct soc_enum cdc_if_tx9_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG2, 2,
			ARRAY_SIZE(cdc_if_tx9_mux_text), cdc_if_tx9_mux_text);
static const struct soc_enum cdc_if_tx10_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG2, 4,
			ARRAY_SIZE(cdc_if_tx10_mux_text), cdc_if_tx10_mux_text);
static const struct soc_enum cdc_if_tx11_inp1_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG3, 0,
			ARRAY_SIZE(cdc_if_tx11_inp1_mux_text),
			cdc_if_tx11_inp1_mux_text);
static const struct soc_enum cdc_if_tx11_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_DATA_HUB_SB_TX11_INP_CFG, 0,
			ARRAY_SIZE(cdc_if_tx11_mux_text), cdc_if_tx11_mux_text);
static const struct soc_enum cdc_if_tx13_inp1_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_CDC_IF_ROUTER_TX_MUX_CFG3, 4,
			ARRAY_SIZE(cdc_if_tx13_inp1_mux_text),
			cdc_if_tx13_inp1_mux_text);
static const struct soc_enum cdc_if_tx13_mux_enum =
	SOC_ENUM_SINGLE(WCD934X_DATA_HUB_SB_TX13_INP_CFG, 0,
			ARRAY_SIZE(cdc_if_tx13_mux_text), cdc_if_tx13_mux_text);

static const struct wcd_mbhc_field wcd_mbhc_fields[WCD_MBHC_REG_FUNC_MAX] = {
	WCD_MBHC_FIELD(WCD_MBHC_L_DET_EN, WCD934X_ANA_MBHC_MECH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_GND_DET_EN, WCD934X_ANA_MBHC_MECH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_MECH_DETECTION_TYPE, WCD934X_ANA_MBHC_MECH, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_CLAMP_CTL, WCD934X_MBHC_NEW_PLUG_DETECT_CTL, 0x30),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_DETECTION_TYPE, WCD934X_ANA_MBHC_ELECT, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_CTRL, WCD934X_MBHC_NEW_PLUG_DETECT_CTL, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL, WCD934X_ANA_MBHC_MECH, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PLUG_TYPE, WCD934X_ANA_MBHC_MECH, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_GND_PLUG_TYPE, WCD934X_ANA_MBHC_MECH, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_SW_HPH_LP_100K_TO_GND, WCD934X_ANA_MBHC_MECH, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_SCHMT_ISRC, WCD934X_ANA_MBHC_ELECT, 0x06),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_EN, WCD934X_ANA_MBHC_ELECT, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_INSREM_DBNC, WCD934X_MBHC_NEW_PLUG_DETECT_CTL, 0x0F),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_DBNC, WCD934X_MBHC_NEW_CTL_1, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_VREF, WCD934X_MBHC_NEW_CTL_2, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_COMP_RESULT, WCD934X_ANA_MBHC_RESULT_3, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_IN2P_CLAMP_STATE, WCD934X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_SCHMT_RESULT, WCD934X_ANA_MBHC_RESULT_3, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_SCHMT_RESULT, WCD934X_ANA_MBHC_RESULT_3, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_SCHMT_RESULT, WCD934X_ANA_MBHC_RESULT_3, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_OCP_FSM_EN, WCD934X_HPH_OCP_CTL, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_RESULT, WCD934X_ANA_MBHC_RESULT_3, 0x07),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_ISRC_CTL, WCD934X_ANA_MBHC_ELECT, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_RESULT, WCD934X_ANA_MBHC_RESULT_3, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB_CTRL, WCD934X_ANA_MICB2, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_CNP_WG_TIME, WCD934X_HPH_CNP_WG_TIME, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_PA_EN, WCD934X_ANA_HPH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PA_EN, WCD934X_ANA_HPH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_PA_EN, WCD934X_ANA_HPH, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_SWCH_LEVEL_REMOVE, WCD934X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_ANC_DET_EN, WCD934X_MBHC_CTL_BCS, 0x02),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_STATUS, WCD934X_MBHC_STATUS_SPARE_1, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_MUX_CTL, WCD934X_MBHC_NEW_CTL_2, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_MOISTURE_STATUS, WCD934X_MBHC_NEW_FSM_STATUS, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_GND, WCD934X_HPH_PA_CTL2, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_GND, WCD934X_HPH_PA_CTL2, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_DET_EN, WCD934X_HPH_L_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_DET_EN, WCD934X_HPH_R_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_STATUS, WCD934X_INTR_PIN1_STATUS0, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_STATUS, WCD934X_INTR_PIN1_STATUS0, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_EN, WCD934X_MBHC_NEW_CTL_1, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_COMPLETE, WCD934X_MBHC_NEW_FSM_STATUS, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_TIMEOUT, WCD934X_MBHC_NEW_FSM_STATUS, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_RESULT, WCD934X_MBHC_NEW_ADC_RESULT, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB2_VOUT, WCD934X_ANA_MICB2, 0x3F),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_MODE, WCD934X_MBHC_NEW_CTL_1, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_DETECTION_DONE, WCD934X_MBHC_NEW_CTL_1, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_ISRC_EN, WCD934X_ANA_MBHC_ZDET, 0x02),
};

static int wcd934x_set_sido_input_src(struct wcd934x_codec *wcd, int sido_src)
{
	if (sido_src == wcd->sido_input_src)
		return 0;

	if (sido_src == SIDO_SOURCE_RCO_BG) {
		regmap_update_bits(wcd->regmap, WCD934X_ANA_RCO,
				   WCD934X_ANA_RCO_BG_EN_MASK,
				   WCD934X_ANA_RCO_BG_ENABLE);
		usleep_range(100, 110);
	}
	wcd->sido_input_src = sido_src;

	return 0;
}

static int wcd934x_enable_ana_bias_and_sysclk(struct wcd934x_codec *wcd)
{
	mutex_lock(&wcd->sysclk_mutex);

	if (++wcd->sysclk_users != 1) {
		mutex_unlock(&wcd->sysclk_mutex);
		return 0;
	}
	mutex_unlock(&wcd->sysclk_mutex);

	regmap_update_bits(wcd->regmap, WCD934X_ANA_BIAS,
			   WCD934X_ANA_BIAS_EN_MASK,
			   WCD934X_ANA_BIAS_EN);
	regmap_update_bits(wcd->regmap, WCD934X_ANA_BIAS,
			   WCD934X_ANA_PRECHRG_EN_MASK,
			   WCD934X_ANA_PRECHRG_EN);
	/*
	 * 1ms delay is required after pre-charge is enabled
	 * as per HW requirement
	 */
	usleep_range(1000, 1100);
	regmap_update_bits(wcd->regmap, WCD934X_ANA_BIAS,
			   WCD934X_ANA_PRECHRG_EN_MASK, 0);
	regmap_update_bits(wcd->regmap, WCD934X_ANA_BIAS,
			   WCD934X_ANA_PRECHRG_MODE_MASK, 0);

	/*
	 * In data clock contrl register is changed
	 * to CLK_SYS_MCLK_PRG
	 */

	regmap_update_bits(wcd->regmap, WCD934X_CLK_SYS_MCLK_PRG,
			   WCD934X_EXT_CLK_BUF_EN_MASK,
			   WCD934X_EXT_CLK_BUF_EN);
	regmap_update_bits(wcd->regmap, WCD934X_CLK_SYS_MCLK_PRG,
			   WCD934X_EXT_CLK_DIV_RATIO_MASK,
			   WCD934X_EXT_CLK_DIV_BY_2);
	regmap_update_bits(wcd->regmap, WCD934X_CLK_SYS_MCLK_PRG,
			   WCD934X_MCLK_SRC_MASK,
			   WCD934X_MCLK_SRC_EXT_CLK);
	regmap_update_bits(wcd->regmap, WCD934X_CLK_SYS_MCLK_PRG,
			   WCD934X_MCLK_EN_MASK, WCD934X_MCLK_EN);
	regmap_update_bits(wcd->regmap,
			   WCD934X_CDC_CLK_RST_CTRL_FS_CNT_CONTROL,
			   WCD934X_CDC_FS_MCLK_CNT_EN_MASK,
			   WCD934X_CDC_FS_MCLK_CNT_ENABLE);
	regmap_update_bits(wcd->regmap,
			   WCD934X_CDC_CLK_RST_CTRL_MCLK_CONTROL,
			   WCD934X_MCLK_EN_MASK,
			   WCD934X_MCLK_EN);
	regmap_update_bits(wcd->regmap, WCD934X_CODEC_RPM_CLK_GATE,
			   WCD934X_CODEC_RPM_CLK_GATE_MASK, 0x0);
	/*
	 * 10us sleep is required after clock is enabled
	 * as per HW requirement
	 */
	usleep_range(10, 15);

	wcd934x_set_sido_input_src(wcd, SIDO_SOURCE_RCO_BG);

	return 0;
}

static int wcd934x_disable_ana_bias_and_syclk(struct wcd934x_codec *wcd)
{
	mutex_lock(&wcd->sysclk_mutex);
	if (--wcd->sysclk_users != 0) {
		mutex_unlock(&wcd->sysclk_mutex);
		return 0;
	}
	mutex_unlock(&wcd->sysclk_mutex);

	regmap_update_bits(wcd->regmap, WCD934X_CLK_SYS_MCLK_PRG,
			   WCD934X_EXT_CLK_BUF_EN_MASK |
			   WCD934X_MCLK_EN_MASK, 0x0);
	regmap_update_bits(wcd->regmap, WCD934X_ANA_BIAS,
			   WCD934X_ANA_BIAS_EN_MASK, 0);
	regmap_update_bits(wcd->regmap, WCD934X_ANA_BIAS,
			   WCD934X_ANA_PRECHRG_EN_MASK, 0);

	return 0;
}

static int __wcd934x_cdc_mclk_enable(struct wcd934x_codec *wcd, bool enable)
{
	int ret = 0;

	if (enable) {
		ret = clk_prepare_enable(wcd->extclk);

		if (ret) {
			dev_err(wcd->dev, "%s: ext clk enable failed\n",
				__func__);
			return ret;
		}
		ret = wcd934x_enable_ana_bias_and_sysclk(wcd);
	} else {
		int val;

		regmap_read(wcd->regmap, WCD934X_CDC_CLK_RST_CTRL_SWR_CONTROL,
			    &val);

		/* Don't disable clock if soundwire using it.*/
		if (val & WCD934X_CDC_SWR_CLK_EN_MASK)
			return 0;

		wcd934x_disable_ana_bias_and_syclk(wcd);
		clk_disable_unprepare(wcd->extclk);
	}

	return ret;
}

static int wcd934x_codec_enable_mclk(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return __wcd934x_cdc_mclk_enable(wcd, true);
	case SND_SOC_DAPM_POST_PMD:
		return __wcd934x_cdc_mclk_enable(wcd, false);
	}

	return 0;
}

static int wcd934x_get_version(struct wcd934x_codec *wcd)
{
	int val1, val2, ver, ret;
	struct regmap *regmap;
	u16 id_minor;
	u32 version_mask = 0;

	regmap = wcd->regmap;
	ver = 0;

	ret = regmap_bulk_read(regmap, WCD934X_CHIP_TIER_CTRL_CHIP_ID_BYTE0,
			       (u8 *)&id_minor, sizeof(u16));

	if (ret)
		return ret;

	regmap_read(regmap, WCD934X_CHIP_TIER_CTRL_EFUSE_VAL_OUT14, &val1);
	regmap_read(regmap, WCD934X_CHIP_TIER_CTRL_EFUSE_VAL_OUT15, &val2);

	version_mask |= (!!((u8)val1 & 0x80)) << DSD_DISABLED_MASK;
	version_mask |= (!!((u8)val2 & 0x01)) << SLNQ_DISABLED_MASK;

	switch (version_mask) {
	case DSD_DISABLED | SLNQ_DISABLED:
		if (id_minor == 0)
			ver = WCD_VERSION_WCD9340_1_0;
		else if (id_minor == 0x01)
			ver = WCD_VERSION_WCD9340_1_1;
		break;
	case SLNQ_DISABLED:
		if (id_minor == 0)
			ver = WCD_VERSION_WCD9341_1_0;
		else if (id_minor == 0x01)
			ver = WCD_VERSION_WCD9341_1_1;
		break;
	}

	wcd->version = ver;
	dev_info(wcd->dev, "WCD934X Minor:0x%x Version:0x%x\n", id_minor, ver);

	return 0;
}

static void wcd934x_enable_efuse_sensing(struct wcd934x_codec *wcd)
{
	int rc, val;

	__wcd934x_cdc_mclk_enable(wcd, true);

	regmap_update_bits(wcd->regmap,
			   WCD934X_CHIP_TIER_CTRL_EFUSE_CTL,
			   WCD934X_EFUSE_SENSE_STATE_MASK,
			   WCD934X_EFUSE_SENSE_STATE_DEF);
	regmap_update_bits(wcd->regmap,
			   WCD934X_CHIP_TIER_CTRL_EFUSE_CTL,
			   WCD934X_EFUSE_SENSE_EN_MASK,
			   WCD934X_EFUSE_SENSE_ENABLE);
	/*
	 * 5ms sleep required after enabling efuse control
	 * before checking the status.
	 */
	usleep_range(5000, 5500);
	wcd934x_set_sido_input_src(wcd, SIDO_SOURCE_RCO_BG);

	rc = regmap_read(wcd->regmap,
			 WCD934X_CHIP_TIER_CTRL_EFUSE_STATUS, &val);
	if (rc || (!(val & 0x01)))
		WARN(1, "%s: Efuse sense is not complete val=%x, ret=%d\n",
		     __func__, val, rc);

	__wcd934x_cdc_mclk_enable(wcd, false);
}

static int wcd934x_swrm_clock(struct wcd934x_codec *wcd, bool enable)
{
	if (enable) {
		__wcd934x_cdc_mclk_enable(wcd, true);
		regmap_update_bits(wcd->regmap,
				   WCD934X_CDC_CLK_RST_CTRL_SWR_CONTROL,
				   WCD934X_CDC_SWR_CLK_EN_MASK,
				   WCD934X_CDC_SWR_CLK_ENABLE);
	} else {
		regmap_update_bits(wcd->regmap,
				   WCD934X_CDC_CLK_RST_CTRL_SWR_CONTROL,
				   WCD934X_CDC_SWR_CLK_EN_MASK, 0);
		__wcd934x_cdc_mclk_enable(wcd, false);
	}

	return 0;
}

static int wcd934x_set_prim_interpolator_rate(struct snd_soc_dai *dai,
					      u8 rate_val, u32 rate)
{
	struct snd_soc_component *comp = dai->component;
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	struct wcd934x_slim_ch *ch;
	u8 cfg0, cfg1, inp0_sel, inp1_sel, inp2_sel;
	int inp, j;

	list_for_each_entry(ch, &wcd->dai[dai->id].slim_ch_list, list) {
		inp = ch->shift + INTn_1_INP_SEL_RX0;
		/*
		 * Loop through all interpolator MUX inputs and find out
		 * to which interpolator input, the slim rx port
		 * is connected
		 */
		for (j = 0; j < WCD934X_NUM_INTERPOLATORS; j++) {
			/* Interpolators 5 and 6 are not aviliable in Tavil */
			if (j == INTERP_LO3_NA || j == INTERP_LO4_NA)
				continue;

			cfg0 = snd_soc_component_read(comp,
					WCD934X_CDC_RX_INP_MUX_RX_INT_CFG0(j));
			cfg1 = snd_soc_component_read(comp,
					WCD934X_CDC_RX_INP_MUX_RX_INT_CFG1(j));

			inp0_sel = cfg0 &
				 WCD934X_CDC_RX_INP_MUX_RX_INT_SEL_MASK;
			inp1_sel = (cfg0 >> 4) &
				 WCD934X_CDC_RX_INP_MUX_RX_INT_SEL_MASK;
			inp2_sel = (cfg1 >> 4) &
				 WCD934X_CDC_RX_INP_MUX_RX_INT_SEL_MASK;

			if ((inp0_sel == inp) ||  (inp1_sel == inp) ||
			    (inp2_sel == inp)) {
				/* rate is in Hz */
				/*
				 * Ear and speaker primary path does not support
				 * native sample rates
				 */
				if ((j == INTERP_EAR || j == INTERP_SPKR1 ||
				     j == INTERP_SPKR2) && rate == 44100)
					dev_err(wcd->dev,
						"Cannot set 44.1KHz on INT%d\n",
						j);
				else
					snd_soc_component_update_bits(comp,
					      WCD934X_CDC_RX_PATH_CTL(j),
					      WCD934X_CDC_MIX_PCM_RATE_MASK,
					      rate_val);
			}
		}
	}

	return 0;
}

static int wcd934x_set_mix_interpolator_rate(struct snd_soc_dai *dai,
					     int rate_val, u32 rate)
{
	struct snd_soc_component *component = dai->component;
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);
	struct wcd934x_slim_ch *ch;
	int val, j;

	list_for_each_entry(ch, &wcd->dai[dai->id].slim_ch_list, list) {
		for (j = 0; j < WCD934X_NUM_INTERPOLATORS; j++) {
			/* Interpolators 5 and 6 are not aviliable in Tavil */
			if (j == INTERP_LO3_NA || j == INTERP_LO4_NA)
				continue;
			val = snd_soc_component_read(component,
					WCD934X_CDC_RX_INP_MUX_RX_INT_CFG1(j)) &
					WCD934X_CDC_RX_INP_MUX_RX_INT_SEL_MASK;

			if (val == (ch->shift + INTn_2_INP_SEL_RX0)) {
				/*
				 * Ear mix path supports only 48, 96, 192,
				 * 384KHz only
				 */
				if ((j == INTERP_EAR) &&
				    (rate_val < 0x4 ||
				     rate_val > 0x7)) {
					dev_err(component->dev,
						"Invalid rate for AIF_PB DAI(%d)\n",
						dai->id);
					return -EINVAL;
				}

				snd_soc_component_update_bits(component,
					      WCD934X_CDC_RX_PATH_MIX_CTL(j),
					      WCD934X_CDC_MIX_PCM_RATE_MASK,
					      rate_val);
			}
		}
	}

	return 0;
}

static int wcd934x_set_interpolator_rate(struct snd_soc_dai *dai,
					 u32 sample_rate)
{
	int rate_val = 0;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(sr_val_tbl); i++) {
		if (sample_rate == sr_val_tbl[i].sample_rate) {
			rate_val = sr_val_tbl[i].rate_val;
			break;
		}
	}
	if ((i == ARRAY_SIZE(sr_val_tbl)) || (rate_val < 0)) {
		dev_err(dai->dev, "Unsupported sample rate: %d\n", sample_rate);
		return -EINVAL;
	}

	ret = wcd934x_set_prim_interpolator_rate(dai, (u8)rate_val,
						 sample_rate);
	if (ret)
		return ret;
	ret = wcd934x_set_mix_interpolator_rate(dai, (u8)rate_val,
						sample_rate);

	return ret;
}

static int wcd934x_set_decimator_rate(struct snd_soc_dai *dai,
				      u8 rate_val, u32 rate)
{
	struct snd_soc_component *comp = dai->component;
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(comp);
	u8 shift = 0, shift_val = 0, tx_mux_sel;
	struct wcd934x_slim_ch *ch;
	int tx_port, tx_port_reg;
	int decimator = -1;

	list_for_each_entry(ch, &wcd->dai[dai->id].slim_ch_list, list) {
		tx_port = ch->port;
		/* Find the SB TX MUX input - which decimator is connected */
		switch (tx_port) {
		case 0 ...  3:
			tx_port_reg = WCD934X_CDC_IF_ROUTER_TX_MUX_CFG0;
			shift = (tx_port << 1);
			shift_val = 0x03;
			break;
		case 4 ... 7:
			tx_port_reg = WCD934X_CDC_IF_ROUTER_TX_MUX_CFG1;
			shift = ((tx_port - 4) << 1);
			shift_val = 0x03;
			break;
		case 8 ... 10:
			tx_port_reg = WCD934X_CDC_IF_ROUTER_TX_MUX_CFG2;
			shift = ((tx_port - 8) << 1);
			shift_val = 0x03;
			break;
		case 11:
			tx_port_reg = WCD934X_CDC_IF_ROUTER_TX_MUX_CFG3;
			shift = 0;
			shift_val = 0x0F;
			break;
		case 13:
			tx_port_reg = WCD934X_CDC_IF_ROUTER_TX_MUX_CFG3;
			shift = 4;
			shift_val = 0x03;
			break;
		default:
			dev_err(wcd->dev, "Invalid SLIM TX%u port DAI ID:%d\n",
				tx_port, dai->id);
			return -EINVAL;
		}

		tx_mux_sel = snd_soc_component_read(comp, tx_port_reg) &
						      (shift_val << shift);

		tx_mux_sel = tx_mux_sel >> shift;
		switch (tx_port) {
		case 0 ... 8:
			if ((tx_mux_sel == 0x2) || (tx_mux_sel == 0x3))
				decimator = tx_port;
			break;
		case 9 ... 10:
			if ((tx_mux_sel == 0x1) || (tx_mux_sel == 0x2))
				decimator = ((tx_port == 9) ? 7 : 6);
			break;
		case 11:
			if ((tx_mux_sel >= 1) && (tx_mux_sel < 7))
				decimator = tx_mux_sel - 1;
			break;
		case 13:
			if ((tx_mux_sel == 0x1) || (tx_mux_sel == 0x2))
				decimator = 5;
			break;
		default:
			dev_err(wcd->dev, "ERROR: Invalid tx_port: %d\n",
				tx_port);
			return -EINVAL;
		}

		snd_soc_component_update_bits(comp,
				      WCD934X_CDC_TX_PATH_CTL(decimator),
				      WCD934X_CDC_TX_PATH_CTL_PCM_RATE_MASK,
				      rate_val);
	}

	return 0;
}

static int wcd934x_slim_set_hw_params(struct wcd934x_codec *wcd,
				      struct wcd_slim_codec_dai_data *dai_data,
				      int direction)
{
	struct list_head *slim_ch_list = &dai_data->slim_ch_list;
	struct slim_stream_config *cfg = &dai_data->sconfig;
	struct wcd934x_slim_ch *ch;
	u16 payload = 0;
	int ret, i;

	cfg->ch_count = 0;
	cfg->direction = direction;
	cfg->port_mask = 0;

	/* Configure slave interface device */
	list_for_each_entry(ch, slim_ch_list, list) {
		cfg->ch_count++;
		payload |= 1 << ch->shift;
		cfg->port_mask |= BIT(ch->port);
	}

	cfg->chs = kcalloc(cfg->ch_count, sizeof(unsigned int), GFP_KERNEL);
	if (!cfg->chs)
		return -ENOMEM;

	i = 0;
	list_for_each_entry(ch, slim_ch_list, list) {
		cfg->chs[i++] = ch->ch_num;
		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			/* write to interface device */
			ret = regmap_write(wcd->if_regmap,
			   WCD934X_SLIM_PGD_RX_PORT_MULTI_CHNL_0(ch->port),
			   payload);

			if (ret < 0)
				goto err;

			/* configure the slave port for water mark and enable*/
			ret = regmap_write(wcd->if_regmap,
					WCD934X_SLIM_PGD_RX_PORT_CFG(ch->port),
					WCD934X_SLIM_WATER_MARK_VAL);
			if (ret < 0)
				goto err;
		} else {
			ret = regmap_write(wcd->if_regmap,
				WCD934X_SLIM_PGD_TX_PORT_MULTI_CHNL_0(ch->port),
				payload & 0x00FF);
			if (ret < 0)
				goto err;

			/* ports 8,9 */
			ret = regmap_write(wcd->if_regmap,
				WCD934X_SLIM_PGD_TX_PORT_MULTI_CHNL_1(ch->port),
				(payload & 0xFF00) >> 8);
			if (ret < 0)
				goto err;

			/* configure the slave port for water mark and enable*/
			ret = regmap_write(wcd->if_regmap,
					WCD934X_SLIM_PGD_TX_PORT_CFG(ch->port),
					WCD934X_SLIM_WATER_MARK_VAL);

			if (ret < 0)
				goto err;
		}
	}

	dai_data->sruntime = slim_stream_allocate(wcd->sdev, "WCD934x-SLIM");

	return 0;

err:
	dev_err(wcd->dev, "Error Setting slim hw params\n");
	kfree(cfg->chs);
	cfg->chs = NULL;

	return ret;
}

static int wcd934x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct wcd934x_codec *wcd;
	int ret, tx_fs_rate = 0;

	wcd = snd_soc_component_get_drvdata(dai->component);

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		ret = wcd934x_set_interpolator_rate(dai, params_rate(params));
		if (ret) {
			dev_err(wcd->dev, "cannot set sample rate: %u\n",
				params_rate(params));
			return ret;
		}
		switch (params_width(params)) {
		case 16 ... 24:
			wcd->dai[dai->id].sconfig.bps = params_width(params);
			break;
		default:
			dev_err(wcd->dev, "Invalid format 0x%x\n",
				params_width(params));
			return -EINVAL;
		}
		break;

	case SNDRV_PCM_STREAM_CAPTURE:
		switch (params_rate(params)) {
		case 8000:
			tx_fs_rate = 0;
			break;
		case 16000:
			tx_fs_rate = 1;
			break;
		case 32000:
			tx_fs_rate = 3;
			break;
		case 48000:
			tx_fs_rate = 4;
			break;
		case 96000:
			tx_fs_rate = 5;
			break;
		case 192000:
			tx_fs_rate = 6;
			break;
		case 384000:
			tx_fs_rate = 7;
			break;
		default:
			dev_err(wcd->dev, "Invalid TX sample rate: %d\n",
				params_rate(params));
			return -EINVAL;

		}

		ret = wcd934x_set_decimator_rate(dai, tx_fs_rate,
						 params_rate(params));
		if (ret < 0) {
			dev_err(wcd->dev, "Cannot set TX Decimator rate\n");
			return ret;
		}
		switch (params_width(params)) {
		case 16 ... 32:
			wcd->dai[dai->id].sconfig.bps = params_width(params);
			break;
		default:
			dev_err(wcd->dev, "Invalid format 0x%x\n",
				params_width(params));
			return -EINVAL;
		}
		break;
	default:
		dev_err(wcd->dev, "Invalid stream type %d\n",
			substream->stream);
		return -EINVAL;
	}

	wcd->dai[dai->id].sconfig.rate = params_rate(params);

	return wcd934x_slim_set_hw_params(wcd, &wcd->dai[dai->id], substream->stream);
}

static int wcd934x_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct wcd_slim_codec_dai_data *dai_data;
	struct wcd934x_codec *wcd;

	wcd = snd_soc_component_get_drvdata(dai->component);

	dai_data = &wcd->dai[dai->id];

	kfree(dai_data->sconfig.chs);

	return 0;
}

static int wcd934x_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct wcd_slim_codec_dai_data *dai_data;
	struct wcd934x_codec *wcd;
	struct slim_stream_config *cfg;

	wcd = snd_soc_component_get_drvdata(dai->component);

	dai_data = &wcd->dai[dai->id];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		cfg = &dai_data->sconfig;
		slim_stream_prepare(dai_data->sruntime, cfg);
		slim_stream_enable(dai_data->sruntime);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		slim_stream_disable(dai_data->sruntime);
		slim_stream_unprepare(dai_data->sruntime);
		break;
	default:
		break;
	}

	return 0;
}

static int wcd934x_set_channel_map(struct snd_soc_dai *dai,
				   unsigned int tx_num,
				   const unsigned int *tx_slot,
				   unsigned int rx_num,
				   const unsigned int *rx_slot)
{
	struct wcd934x_codec *wcd;
	int i;

	wcd = snd_soc_component_get_drvdata(dai->component);

	if (tx_num > WCD934X_TX_MAX || rx_num > WCD934X_RX_MAX) {
		dev_err(wcd->dev, "Invalid tx %d or rx %d channel count\n",
			tx_num, rx_num);
		return -EINVAL;
	}

	if (!tx_slot || !rx_slot) {
		dev_err(wcd->dev, "Invalid tx_slot=%p, rx_slot=%p\n",
			tx_slot, rx_slot);
		return -EINVAL;
	}

	wcd->num_rx_port = rx_num;
	for (i = 0; i < rx_num; i++) {
		wcd->rx_chs[i].ch_num = rx_slot[i];
		INIT_LIST_HEAD(&wcd->rx_chs[i].list);
	}

	wcd->num_tx_port = tx_num;
	for (i = 0; i < tx_num; i++) {
		wcd->tx_chs[i].ch_num = tx_slot[i];
		INIT_LIST_HEAD(&wcd->tx_chs[i].list);
	}

	return 0;
}

static int wcd934x_get_channel_map(const struct snd_soc_dai *dai,
				   unsigned int *tx_num, unsigned int *tx_slot,
				   unsigned int *rx_num, unsigned int *rx_slot)
{
	struct wcd934x_slim_ch *ch;
	struct wcd934x_codec *wcd;
	int i = 0;

	wcd = snd_soc_component_get_drvdata(dai->component);

	switch (dai->id) {
	case AIF1_PB:
	case AIF2_PB:
	case AIF3_PB:
	case AIF4_PB:
		if (!rx_slot || !rx_num) {
			dev_err(wcd->dev, "Invalid rx_slot %p or rx_num %p\n",
				rx_slot, rx_num);
			return -EINVAL;
		}

		list_for_each_entry(ch, &wcd->dai[dai->id].slim_ch_list, list)
			rx_slot[i++] = ch->ch_num;

		*rx_num = i;
		break;
	case AIF1_CAP:
	case AIF2_CAP:
	case AIF3_CAP:
		if (!tx_slot || !tx_num) {
			dev_err(wcd->dev, "Invalid tx_slot %p or tx_num %p\n",
				tx_slot, tx_num);
			return -EINVAL;
		}

		list_for_each_entry(ch, &wcd->dai[dai->id].slim_ch_list, list)
			tx_slot[i++] = ch->ch_num;

		*tx_num = i;
		break;
	default:
		dev_err(wcd->dev, "Invalid DAI ID %x\n", dai->id);
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops wcd934x_dai_ops = {
	.hw_params = wcd934x_hw_params,
	.hw_free = wcd934x_hw_free,
	.trigger = wcd934x_trigger,
	.set_channel_map = wcd934x_set_channel_map,
	.get_channel_map = wcd934x_get_channel_map,
};

static struct snd_soc_dai_driver wcd934x_slim_dais[] = {
	[0] = {
		.name = "wcd934x_rx1",
		.id = AIF1_PB,
		.playback = {
			.stream_name = "AIF1 Playback",
			.rates = WCD934X_RATES_MASK | WCD934X_FRAC_RATES_MASK,
			.formats = WCD934X_FORMATS_S16_S24_LE,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd934x_dai_ops,
	},
	[1] = {
		.name = "wcd934x_tx1",
		.id = AIF1_CAP,
		.capture = {
			.stream_name = "AIF1 Capture",
			.rates = WCD934X_RATES_MASK,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd934x_dai_ops,
	},
	[2] = {
		.name = "wcd934x_rx2",
		.id = AIF2_PB,
		.playback = {
			.stream_name = "AIF2 Playback",
			.rates = WCD934X_RATES_MASK | WCD934X_FRAC_RATES_MASK,
			.formats = WCD934X_FORMATS_S16_S24_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd934x_dai_ops,
	},
	[3] = {
		.name = "wcd934x_tx2",
		.id = AIF2_CAP,
		.capture = {
			.stream_name = "AIF2 Capture",
			.rates = WCD934X_RATES_MASK,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd934x_dai_ops,
	},
	[4] = {
		.name = "wcd934x_rx3",
		.id = AIF3_PB,
		.playback = {
			.stream_name = "AIF3 Playback",
			.rates = WCD934X_RATES_MASK | WCD934X_FRAC_RATES_MASK,
			.formats = WCD934X_FORMATS_S16_S24_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd934x_dai_ops,
	},
	[5] = {
		.name = "wcd934x_tx3",
		.id = AIF3_CAP,
		.capture = {
			.stream_name = "AIF3 Capture",
			.rates = WCD934X_RATES_MASK,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd934x_dai_ops,
	},
	[6] = {
		.name = "wcd934x_rx4",
		.id = AIF4_PB,
		.playback = {
			.stream_name = "AIF4 Playback",
			.rates = WCD934X_RATES_MASK | WCD934X_FRAC_RATES_MASK,
			.formats = WCD934X_FORMATS_S16_S24_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd934x_dai_ops,
	},
};

static int swclk_gate_enable(struct clk_hw *hw)
{
	return wcd934x_swrm_clock(to_wcd934x_codec(hw), true);
}

static void swclk_gate_disable(struct clk_hw *hw)
{
	wcd934x_swrm_clock(to_wcd934x_codec(hw), false);
}

static int swclk_gate_is_enabled(struct clk_hw *hw)
{
	struct wcd934x_codec *wcd = to_wcd934x_codec(hw);
	int ret, val;

	regmap_read(wcd->regmap, WCD934X_CDC_CLK_RST_CTRL_SWR_CONTROL, &val);
	ret = val & WCD934X_CDC_SWR_CLK_EN_MASK;

	return ret;
}

static unsigned long swclk_recalc_rate(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	return parent_rate / 2;
}

static const struct clk_ops swclk_gate_ops = {
	.prepare = swclk_gate_enable,
	.unprepare = swclk_gate_disable,
	.is_enabled = swclk_gate_is_enabled,
	.recalc_rate = swclk_recalc_rate,

};

static struct clk *wcd934x_register_mclk_output(struct wcd934x_codec *wcd)
{
	struct clk *parent = wcd->extclk;
	struct device *dev = wcd->dev;
	struct device_node *np = dev->parent->of_node;
	const char *parent_clk_name = NULL;
	const char *clk_name = "mclk";
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	if (of_property_read_u32(np, "clock-frequency", &wcd->rate))
		return NULL;

	parent_clk_name = __clk_get_name(parent);

	of_property_read_string(np, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = &swclk_gate_ops;
	init.flags = 0;
	init.parent_names = &parent_clk_name;
	init.num_parents = 1;
	wcd->hw.init = &init;

	hw = &wcd->hw;
	ret = devm_clk_hw_register(wcd->dev->parent, hw);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
	if (ret)
		return ERR_PTR(ret);

	return NULL;
}

static int wcd934x_get_micbias_val(struct device *dev, const char *micbias,
				   u32 *micb_mv)
{
	int mv;

	if (of_property_read_u32(dev->parent->of_node, micbias, &mv)) {
		dev_err(dev, "%s value not found, using default\n", micbias);
		mv = WCD934X_DEF_MICBIAS_MV;
	} else {
		/* convert it to milli volts */
		mv = mv/1000;
	}

	if (mv < 1000 || mv > 2850) {
		dev_err(dev, "%s value not in valid range, using default\n",
			micbias);
		mv = WCD934X_DEF_MICBIAS_MV;
	}

	if (micb_mv)
		*micb_mv = mv;

	return (mv - 1000) / 50;
}

static int wcd934x_init_dmic(struct snd_soc_component *comp)
{
	int vout_ctl_1, vout_ctl_2, vout_ctl_3, vout_ctl_4;
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	u32 def_dmic_rate, dmic_clk_drv;

	vout_ctl_1 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias1-microvolt", NULL);
	vout_ctl_2 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias2-microvolt",
					     &wcd->micb2_mv);
	vout_ctl_3 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias3-microvolt", NULL);
	vout_ctl_4 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias4-microvolt", NULL);

	snd_soc_component_update_bits(comp, WCD934X_ANA_MICB1,
				      WCD934X_MICB_VAL_MASK, vout_ctl_1);
	snd_soc_component_update_bits(comp, WCD934X_ANA_MICB2,
				      WCD934X_MICB_VAL_MASK, vout_ctl_2);
	snd_soc_component_update_bits(comp, WCD934X_ANA_MICB3,
				      WCD934X_MICB_VAL_MASK, vout_ctl_3);
	snd_soc_component_update_bits(comp, WCD934X_ANA_MICB4,
				      WCD934X_MICB_VAL_MASK, vout_ctl_4);

	if (wcd->rate == WCD934X_MCLK_CLK_9P6MHZ)
		def_dmic_rate = WCD9XXX_DMIC_SAMPLE_RATE_4P8MHZ;
	else
		def_dmic_rate = WCD9XXX_DMIC_SAMPLE_RATE_4P096MHZ;

	wcd->dmic_sample_rate = def_dmic_rate;

	dmic_clk_drv = 0;
	snd_soc_component_update_bits(comp, WCD934X_TEST_DEBUG_PAD_DRVCTL_0,
				      0x0C, dmic_clk_drv << 2);

	return 0;
}

static void wcd934x_hw_init(struct wcd934x_codec *wcd)
{
	struct regmap *rm = wcd->regmap;

	/* set SPKR rate to FS_2P4_3P072 */
	regmap_update_bits(rm, WCD934X_CDC_RX7_RX_PATH_CFG1, 0x08, 0x08);
	regmap_update_bits(rm, WCD934X_CDC_RX8_RX_PATH_CFG1, 0x08, 0x08);

	/* Take DMICs out of reset */
	regmap_update_bits(rm, WCD934X_CPE_SS_DMIC_CFG, 0x80, 0x00);
}

static int wcd934x_comp_init(struct snd_soc_component *component)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);

	wcd934x_hw_init(wcd);
	wcd934x_enable_efuse_sensing(wcd);
	wcd934x_get_version(wcd);

	return 0;
}

static irqreturn_t wcd934x_slim_irq_handler(int irq, void *data)
{
	struct wcd934x_codec *wcd = data;
	unsigned long status = 0;
	int i, j, port_id;
	unsigned int val, int_val = 0;
	irqreturn_t ret = IRQ_NONE;
	bool tx;
	unsigned short reg = 0;

	for (i = WCD934X_SLIM_PGD_PORT_INT_STATUS_RX_0, j = 0;
	     i <= WCD934X_SLIM_PGD_PORT_INT_STATUS_TX_1; i++, j++) {
		regmap_read(wcd->if_regmap, i, &val);
		status |= ((u32)val << (8 * j));
	}

	for_each_set_bit(j, &status, 32) {
		tx = false;
		port_id = j;

		if (j >= 16) {
			tx = true;
			port_id = j - 16;
		}

		regmap_read(wcd->if_regmap,
			    WCD934X_SLIM_PGD_PORT_INT_RX_SOURCE0 + j, &val);
		if (val) {
			if (!tx)
				reg = WCD934X_SLIM_PGD_PORT_INT_EN0 +
					(port_id / 8);
			else
				reg = WCD934X_SLIM_PGD_PORT_INT_TX_EN0 +
					(port_id / 8);
			regmap_read(wcd->if_regmap, reg, &int_val);
		}

		if (val & WCD934X_SLIM_IRQ_OVERFLOW)
			dev_err_ratelimited(wcd->dev,
					    "overflow error on %s port %d, value %x\n",
					    (tx ? "TX" : "RX"), port_id, val);

		if (val & WCD934X_SLIM_IRQ_UNDERFLOW)
			dev_err_ratelimited(wcd->dev,
					    "underflow error on %s port %d, value %x\n",
					    (tx ? "TX" : "RX"), port_id, val);

		if ((val & WCD934X_SLIM_IRQ_OVERFLOW) ||
		    (val & WCD934X_SLIM_IRQ_UNDERFLOW)) {
			if (!tx)
				reg = WCD934X_SLIM_PGD_PORT_INT_EN0 +
					(port_id / 8);
			else
				reg = WCD934X_SLIM_PGD_PORT_INT_TX_EN0 +
					(port_id / 8);
			regmap_read(
				wcd->if_regmap, reg, &int_val);
			if (int_val & (1 << (port_id % 8))) {
				int_val = int_val ^ (1 << (port_id % 8));
				regmap_write(wcd->if_regmap,
					     reg, int_val);
			}
		}

		if (val & WCD934X_SLIM_IRQ_PORT_CLOSED)
			dev_err_ratelimited(wcd->dev,
					    "Port Closed %s port %d, value %x\n",
					    (tx ? "TX" : "RX"), port_id, val);

		regmap_write(wcd->if_regmap,
			     WCD934X_SLIM_PGD_PORT_INT_CLR_RX_0 + (j / 8),
				BIT(j % 8));
		ret = IRQ_HANDLED;
	}

	return ret;
}

static void wcd934x_mbhc_clk_setup(struct snd_soc_component *component,
				   bool enable)
{
	snd_soc_component_write_field(component, WCD934X_MBHC_NEW_CTL_1,
				      WCD934X_MBHC_CTL_RCO_EN_MASK, enable);
}

static void wcd934x_mbhc_mbhc_bias_control(struct snd_soc_component *component,
					   bool enable)
{
	snd_soc_component_write_field(component, WCD934X_ANA_MBHC_ELECT,
				      WCD934X_ANA_MBHC_BIAS_EN, enable);
}

static void wcd934x_mbhc_program_btn_thr(struct snd_soc_component *component,
					 int *btn_low, int *btn_high,
					 int num_btn, bool is_micbias)
{
	int i, vth;

	if (num_btn > WCD_MBHC_DEF_BUTTONS) {
		dev_err(component->dev, "%s: invalid number of buttons: %d\n",
			__func__, num_btn);
		return;
	}

	for (i = 0; i < num_btn; i++) {
		vth = ((btn_high[i] * 2) / 25) & 0x3F;
		snd_soc_component_write_field(component, WCD934X_ANA_MBHC_BTN0 + i,
					   WCD934X_MBHC_BTN_VTH_MASK, vth);
	}
}

static bool wcd934x_mbhc_micb_en_status(struct snd_soc_component *component, int micb_num)
{
	u8 val;

	if (micb_num == MIC_BIAS_2) {
		val = snd_soc_component_read_field(component, WCD934X_ANA_MICB2,
						   WCD934X_ANA_MICB2_ENABLE_MASK);
		if (val == WCD934X_MICB_ENABLE)
			return true;
	}
	return false;
}

static void wcd934x_mbhc_hph_l_pull_up_control(struct snd_soc_component *component,
					       enum mbhc_hs_pullup_iref pull_up_cur)
{
	/* Default pull up current to 2uA */
	if (pull_up_cur < I_OFF || pull_up_cur > I_3P0_UA ||
	    pull_up_cur == I_DEFAULT)
		pull_up_cur = I_2P0_UA;


	snd_soc_component_write_field(component, WCD934X_MBHC_NEW_PLUG_DETECT_CTL,
				      WCD934X_HSDET_PULLUP_C_MASK, pull_up_cur);
}

static int wcd934x_micbias_control(struct snd_soc_component *component,
			    int micb_num, int req, bool is_dapm)
{
	struct wcd934x_codec *wcd934x = snd_soc_component_get_drvdata(component);
	int micb_index = micb_num - 1;
	u16 micb_reg;

	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD934X_ANA_MICB1;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD934X_ANA_MICB2;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD934X_ANA_MICB3;
		break;
	case MIC_BIAS_4:
		micb_reg = WCD934X_ANA_MICB4;
		break;
	default:
		dev_err(component->dev, "%s: Invalid micbias number: %d\n",
			__func__, micb_num);
		return -EINVAL;
	}
	mutex_lock(&wcd934x->micb_lock);

	switch (req) {
	case MICB_PULLUP_ENABLE:
		wcd934x->pullup_ref[micb_index]++;
		if ((wcd934x->pullup_ref[micb_index] == 1) &&
		    (wcd934x->micb_ref[micb_index] == 0))
			snd_soc_component_write_field(component, micb_reg,
						      WCD934X_ANA_MICB_EN_MASK,
						      WCD934X_MICB_PULL_UP);
		break;
	case MICB_PULLUP_DISABLE:
		if (wcd934x->pullup_ref[micb_index] > 0)
			wcd934x->pullup_ref[micb_index]--;

		if ((wcd934x->pullup_ref[micb_index] == 0) &&
		    (wcd934x->micb_ref[micb_index] == 0))
			snd_soc_component_write_field(component, micb_reg,
						      WCD934X_ANA_MICB_EN_MASK, 0);
		break;
	case MICB_ENABLE:
		wcd934x->micb_ref[micb_index]++;
		if (wcd934x->micb_ref[micb_index] == 1) {
			snd_soc_component_write_field(component, micb_reg,
						      WCD934X_ANA_MICB_EN_MASK,
						      WCD934X_MICB_ENABLE);
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd934x->mbhc,
						      WCD_EVENT_POST_MICBIAS_2_ON);
		}

		if (micb_num  == MIC_BIAS_2 && is_dapm)
			wcd_mbhc_event_notify(wcd934x->mbhc,
					      WCD_EVENT_POST_DAPM_MICBIAS_2_ON);
		break;
	case MICB_DISABLE:
		if (wcd934x->micb_ref[micb_index] > 0)
			wcd934x->micb_ref[micb_index]--;

		if ((wcd934x->micb_ref[micb_index] == 0) &&
		    (wcd934x->pullup_ref[micb_index] > 0))
			snd_soc_component_write_field(component, micb_reg,
						      WCD934X_ANA_MICB_EN_MASK,
						      WCD934X_MICB_PULL_UP);
		else if ((wcd934x->micb_ref[micb_index] == 0) &&
			 (wcd934x->pullup_ref[micb_index] == 0)) {
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd934x->mbhc,
						      WCD_EVENT_PRE_MICBIAS_2_OFF);

			snd_soc_component_write_field(component, micb_reg,
						      WCD934X_ANA_MICB_EN_MASK, 0);
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd934x->mbhc,
						      WCD_EVENT_POST_MICBIAS_2_OFF);
		}
		if (is_dapm && micb_num  == MIC_BIAS_2)
			wcd_mbhc_event_notify(wcd934x->mbhc,
					      WCD_EVENT_POST_DAPM_MICBIAS_2_OFF);
		break;
	}

	mutex_unlock(&wcd934x->micb_lock);

	return 0;
}

static int wcd934x_mbhc_request_micbias(struct snd_soc_component *component,
					int micb_num, int req)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);
	int ret;

	if (req == MICB_ENABLE)
		__wcd934x_cdc_mclk_enable(wcd, true);

	ret = wcd934x_micbias_control(component, micb_num, req, false);

	if (req == MICB_DISABLE)
		__wcd934x_cdc_mclk_enable(wcd, false);

	return ret;
}

static void wcd934x_mbhc_micb_ramp_control(struct snd_soc_component *component,
					   bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD934X_ANA_MICB2_RAMP,
				    WCD934X_RAMP_SHIFT_CTRL_MASK, 0x3);
		snd_soc_component_write_field(component, WCD934X_ANA_MICB2_RAMP,
				    WCD934X_RAMP_EN_MASK, 1);
	} else {
		snd_soc_component_write_field(component, WCD934X_ANA_MICB2_RAMP,
				    WCD934X_RAMP_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD934X_ANA_MICB2_RAMP,
				    WCD934X_RAMP_SHIFT_CTRL_MASK, 0);
	}
}

static int wcd934x_get_micb_vout_ctl_val(u32 micb_mv)
{
	/* min micbias voltage is 1V and maximum is 2.85V */
	if (micb_mv < 1000 || micb_mv > 2850)
		return -EINVAL;

	return (micb_mv - 1000) / 50;
}

static int wcd934x_mbhc_micb_adjust_voltage(struct snd_soc_component *component,
					    int req_volt, int micb_num)
{
	struct wcd934x_codec *wcd934x = snd_soc_component_get_drvdata(component);
	int cur_vout_ctl, req_vout_ctl, micb_reg, micb_en, ret = 0;

	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD934X_ANA_MICB1;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD934X_ANA_MICB2;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD934X_ANA_MICB3;
		break;
	case MIC_BIAS_4:
		micb_reg = WCD934X_ANA_MICB4;
		break;
	default:
		return -EINVAL;
	}
	mutex_lock(&wcd934x->micb_lock);
	/*
	 * If requested micbias voltage is same as current micbias
	 * voltage, then just return. Otherwise, adjust voltage as
	 * per requested value. If micbias is already enabled, then
	 * to avoid slow micbias ramp-up or down enable pull-up
	 * momentarily, change the micbias value and then re-enable
	 * micbias.
	 */
	micb_en = snd_soc_component_read_field(component, micb_reg,
						WCD934X_ANA_MICB_EN_MASK);
	cur_vout_ctl = snd_soc_component_read_field(component, micb_reg,
						    WCD934X_MICB_VAL_MASK);

	req_vout_ctl = wcd934x_get_micb_vout_ctl_val(req_volt);
	if (req_vout_ctl < 0) {
		ret = -EINVAL;
		goto exit;
	}

	if (cur_vout_ctl == req_vout_ctl) {
		ret = 0;
		goto exit;
	}

	if (micb_en == WCD934X_MICB_ENABLE)
		snd_soc_component_write_field(component, micb_reg,
					      WCD934X_ANA_MICB_EN_MASK,
					      WCD934X_MICB_PULL_UP);

	snd_soc_component_write_field(component, micb_reg,
				      WCD934X_MICB_VAL_MASK,
				      req_vout_ctl);

	if (micb_en == WCD934X_MICB_ENABLE) {
		snd_soc_component_write_field(component, micb_reg,
					      WCD934X_ANA_MICB_EN_MASK,
					      WCD934X_MICB_ENABLE);
		/*
		 * Add 2ms delay as per HW requirement after enabling
		 * micbias
		 */
		usleep_range(2000, 2100);
	}
exit:
	mutex_unlock(&wcd934x->micb_lock);
	return ret;
}

static int wcd934x_mbhc_micb_ctrl_threshold_mic(struct snd_soc_component *component,
						int micb_num, bool req_en)
{
	struct wcd934x_codec *wcd934x = snd_soc_component_get_drvdata(component);
	int rc, micb_mv;

	if (micb_num != MIC_BIAS_2)
		return -EINVAL;
	/*
	 * If device tree micbias level is already above the minimum
	 * voltage needed to detect threshold microphone, then do
	 * not change the micbias, just return.
	 */
	if (wcd934x->micb2_mv >= WCD_MBHC_THR_HS_MICB_MV)
		return 0;

	micb_mv = req_en ? WCD_MBHC_THR_HS_MICB_MV : wcd934x->micb2_mv;

	rc = wcd934x_mbhc_micb_adjust_voltage(component, micb_mv, MIC_BIAS_2);

	return rc;
}

static void wcd934x_mbhc_get_result_params(struct wcd934x_codec *wcd934x,
						s16 *d1_a, u16 noff,
						int32_t *zdet)
{
	int i;
	int val, val1;
	s16 c1;
	s32 x1, d1;
	int32_t denom;
	static const int minCode_param[] = {
		3277, 1639, 820, 410, 205, 103, 52, 26
	};

	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ZDET, 0x20, 0x20);
	for (i = 0; i < WCD934X_ZDET_NUM_MEASUREMENTS; i++) {
		regmap_read(wcd934x->regmap, WCD934X_ANA_MBHC_RESULT_2, &val);
		if (val & 0x80)
			break;
	}
	val = val << 0x8;
	regmap_read(wcd934x->regmap, WCD934X_ANA_MBHC_RESULT_1, &val1);
	val |= val1;
	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ZDET, 0x20, 0x00);
	x1 = WCD934X_MBHC_GET_X1(val);
	c1 = WCD934X_MBHC_GET_C1(val);
	/* If ramp is not complete, give additional 5ms */
	if ((c1 < 2) && x1)
		usleep_range(5000, 5050);

	if (!c1 || !x1) {
		dev_err(wcd934x->dev, "%s: Impedance detect ramp error, c1=%d, x1=0x%x\n",
			__func__, c1, x1);
		goto ramp_down;
	}
	d1 = d1_a[c1];
	denom = (x1 * d1) - (1 << (14 - noff));
	if (denom > 0)
		*zdet = (WCD934X_MBHC_ZDET_CONST * 1000) / denom;
	else if (x1 < minCode_param[noff])
		*zdet = WCD934X_ZDET_FLOATING_IMPEDANCE;

	dev_dbg(wcd934x->dev, "%s: d1=%d, c1=%d, x1=0x%x, z_val=%di (milliohm)\n",
		__func__, d1, c1, x1, *zdet);
ramp_down:
	i = 0;

	while (x1) {
		regmap_read(wcd934x->regmap, WCD934X_ANA_MBHC_RESULT_1, &val);
		regmap_read(wcd934x->regmap, WCD934X_ANA_MBHC_RESULT_2, &val1);
		val = val << 0x08;
		val |= val1;
		x1 = WCD934X_MBHC_GET_X1(val);
		i++;
		if (i == WCD934X_ZDET_NUM_MEASUREMENTS)
			break;
	}
}

static void wcd934x_mbhc_zdet_ramp(struct snd_soc_component *component,
				 struct wcd934x_mbhc_zdet_param *zdet_param,
				 int32_t *zl, int32_t *zr, s16 *d1_a)
{
	struct wcd934x_codec *wcd934x = dev_get_drvdata(component->dev);
	int32_t zdet = 0;

	snd_soc_component_write_field(component, WCD934X_MBHC_NEW_ZDET_ANA_CTL,
				WCD934X_ZDET_MAXV_CTL_MASK, zdet_param->ldo_ctl);
	snd_soc_component_update_bits(component, WCD934X_ANA_MBHC_BTN5,
				    WCD934X_VTH_MASK, zdet_param->btn5);
	snd_soc_component_update_bits(component, WCD934X_ANA_MBHC_BTN6,
				      WCD934X_VTH_MASK, zdet_param->btn6);
	snd_soc_component_update_bits(component, WCD934X_ANA_MBHC_BTN7,
				     WCD934X_VTH_MASK, zdet_param->btn7);
	snd_soc_component_write_field(component, WCD934X_MBHC_NEW_ZDET_ANA_CTL,
				WCD934X_ZDET_RANGE_CTL_MASK, zdet_param->noff);
	snd_soc_component_update_bits(component, WCD934X_MBHC_NEW_ZDET_RAMP_CTL,
				0x0F, zdet_param->nshift);

	if (!zl)
		goto z_right;
	/* Start impedance measurement for HPH_L */
	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ZDET, 0x80, 0x80);
	wcd934x_mbhc_get_result_params(wcd934x, d1_a, zdet_param->noff, &zdet);
	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ZDET, 0x80, 0x00);

	*zl = zdet;

z_right:
	if (!zr)
		return;
	/* Start impedance measurement for HPH_R */
	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ZDET, 0x40, 0x40);
	wcd934x_mbhc_get_result_params(wcd934x, d1_a, zdet_param->noff, &zdet);
	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ZDET, 0x40, 0x00);

	*zr = zdet;
}

static void wcd934x_wcd_mbhc_qfuse_cal(struct snd_soc_component *component,
					int32_t *z_val, int flag_l_r)
{
	s16 q1;
	int q1_cal;

	if (*z_val < (WCD934X_ZDET_VAL_400/1000))
		q1 = snd_soc_component_read(component,
			WCD934X_CHIP_TIER_CTRL_EFUSE_VAL_OUT1 + (2 * flag_l_r));
	else
		q1 = snd_soc_component_read(component,
			WCD934X_CHIP_TIER_CTRL_EFUSE_VAL_OUT2 + (2 * flag_l_r));
	if (q1 & 0x80)
		q1_cal = (10000 - ((q1 & 0x7F) * 25));
	else
		q1_cal = (10000 + (q1 * 25));
	if (q1_cal > 0)
		*z_val = ((*z_val) * 10000) / q1_cal;
}

static void wcd934x_wcd_mbhc_calc_impedance(struct snd_soc_component *component,
					    uint32_t *zl, uint32_t *zr)
{
	struct wcd934x_codec *wcd934x = dev_get_drvdata(component->dev);
	s16 reg0, reg1, reg2, reg3, reg4;
	int32_t z1L, z1R, z1Ls;
	int zMono, z_diff1, z_diff2;
	bool is_fsm_disable = false;
	struct wcd934x_mbhc_zdet_param zdet_param[] = {
		{4, 0, 4, 0x08, 0x14, 0x18}, /* < 32ohm */
		{2, 0, 3, 0x18, 0x7C, 0x90}, /* 32ohm < Z < 400ohm */
		{1, 4, 5, 0x18, 0x7C, 0x90}, /* 400ohm < Z < 1200ohm */
		{1, 6, 7, 0x18, 0x7C, 0x90}, /* >1200ohm */
	};
	struct wcd934x_mbhc_zdet_param *zdet_param_ptr = NULL;
	s16 d1_a[][4] = {
		{0, 30, 90, 30},
		{0, 30, 30, 5},
		{0, 30, 30, 5},
		{0, 30, 30, 5},
	};
	s16 *d1 = NULL;

	reg0 = snd_soc_component_read(component, WCD934X_ANA_MBHC_BTN5);
	reg1 = snd_soc_component_read(component, WCD934X_ANA_MBHC_BTN6);
	reg2 = snd_soc_component_read(component, WCD934X_ANA_MBHC_BTN7);
	reg3 = snd_soc_component_read(component, WCD934X_MBHC_CTL_CLK);
	reg4 = snd_soc_component_read(component, WCD934X_MBHC_NEW_ZDET_ANA_CTL);

	if (snd_soc_component_read(component, WCD934X_ANA_MBHC_ELECT) & 0x80) {
		is_fsm_disable = true;
		regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ELECT, 0x80, 0x00);
	}

	/* For NO-jack, disable L_DET_EN before Z-det measurements */
	if (wcd934x->mbhc_cfg.hphl_swh)
		regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_MECH, 0x80, 0x00);

	/* Turn off 100k pull down on HPHL */
	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_MECH, 0x01, 0x00);

	/* First get impedance on Left */
	d1 = d1_a[1];
	zdet_param_ptr = &zdet_param[1];
	wcd934x_mbhc_zdet_ramp(component, zdet_param_ptr, &z1L, NULL, d1);

	if (!WCD934X_MBHC_IS_SECOND_RAMP_REQUIRED(z1L))
		goto left_ch_impedance;

	/* Second ramp for left ch */
	if (z1L < WCD934X_ZDET_VAL_32) {
		zdet_param_ptr = &zdet_param[0];
		d1 = d1_a[0];
	} else if ((z1L > WCD934X_ZDET_VAL_400) &&
		  (z1L <= WCD934X_ZDET_VAL_1200)) {
		zdet_param_ptr = &zdet_param[2];
		d1 = d1_a[2];
	} else if (z1L > WCD934X_ZDET_VAL_1200) {
		zdet_param_ptr = &zdet_param[3];
		d1 = d1_a[3];
	}
	wcd934x_mbhc_zdet_ramp(component, zdet_param_ptr, &z1L, NULL, d1);

left_ch_impedance:
	if ((z1L == WCD934X_ZDET_FLOATING_IMPEDANCE) ||
		(z1L > WCD934X_ZDET_VAL_100K)) {
		*zl = WCD934X_ZDET_FLOATING_IMPEDANCE;
		zdet_param_ptr = &zdet_param[1];
		d1 = d1_a[1];
	} else {
		*zl = z1L/1000;
		wcd934x_wcd_mbhc_qfuse_cal(component, zl, 0);
	}
	dev_info(component->dev, "%s: impedance on HPH_L = %d(ohms)\n",
		__func__, *zl);

	/* Start of right impedance ramp and calculation */
	wcd934x_mbhc_zdet_ramp(component, zdet_param_ptr, NULL, &z1R, d1);
	if (WCD934X_MBHC_IS_SECOND_RAMP_REQUIRED(z1R)) {
		if (((z1R > WCD934X_ZDET_VAL_1200) &&
			(zdet_param_ptr->noff == 0x6)) ||
			((*zl) != WCD934X_ZDET_FLOATING_IMPEDANCE))
			goto right_ch_impedance;
		/* Second ramp for right ch */
		if (z1R < WCD934X_ZDET_VAL_32) {
			zdet_param_ptr = &zdet_param[0];
			d1 = d1_a[0];
		} else if ((z1R > WCD934X_ZDET_VAL_400) &&
			(z1R <= WCD934X_ZDET_VAL_1200)) {
			zdet_param_ptr = &zdet_param[2];
			d1 = d1_a[2];
		} else if (z1R > WCD934X_ZDET_VAL_1200) {
			zdet_param_ptr = &zdet_param[3];
			d1 = d1_a[3];
		}
		wcd934x_mbhc_zdet_ramp(component, zdet_param_ptr, NULL, &z1R, d1);
	}
right_ch_impedance:
	if ((z1R == WCD934X_ZDET_FLOATING_IMPEDANCE) ||
		(z1R > WCD934X_ZDET_VAL_100K)) {
		*zr = WCD934X_ZDET_FLOATING_IMPEDANCE;
	} else {
		*zr = z1R/1000;
		wcd934x_wcd_mbhc_qfuse_cal(component, zr, 1);
	}
	dev_err(component->dev, "%s: impedance on HPH_R = %d(ohms)\n",
		__func__, *zr);

	/* Mono/stereo detection */
	if ((*zl == WCD934X_ZDET_FLOATING_IMPEDANCE) &&
		(*zr == WCD934X_ZDET_FLOATING_IMPEDANCE)) {
		dev_dbg(component->dev,
			"%s: plug type is invalid or extension cable\n",
			__func__);
		goto zdet_complete;
	}
	if ((*zl == WCD934X_ZDET_FLOATING_IMPEDANCE) ||
	    (*zr == WCD934X_ZDET_FLOATING_IMPEDANCE) ||
	    ((*zl < WCD_MONO_HS_MIN_THR) && (*zr > WCD_MONO_HS_MIN_THR)) ||
	    ((*zl > WCD_MONO_HS_MIN_THR) && (*zr < WCD_MONO_HS_MIN_THR))) {
		dev_dbg(component->dev,
			"%s: Mono plug type with one ch floating or shorted to GND\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd934x->mbhc, WCD_MBHC_HPH_MONO);
		goto zdet_complete;
	}
	snd_soc_component_write_field(component, WCD934X_HPH_R_ATEST,
				      WCD934X_HPHPA_GND_OVR_MASK, 1);
	snd_soc_component_write_field(component, WCD934X_HPH_PA_CTL2,
				      WCD934X_HPHPA_GND_R_MASK, 1);
	if (*zl < (WCD934X_ZDET_VAL_32/1000))
		wcd934x_mbhc_zdet_ramp(component, &zdet_param[0], &z1Ls, NULL, d1);
	else
		wcd934x_mbhc_zdet_ramp(component, &zdet_param[1], &z1Ls, NULL, d1);
	snd_soc_component_write_field(component, WCD934X_HPH_PA_CTL2,
				      WCD934X_HPHPA_GND_R_MASK, 0);
	snd_soc_component_write_field(component, WCD934X_HPH_R_ATEST,
				      WCD934X_HPHPA_GND_OVR_MASK, 0);
	z1Ls /= 1000;
	wcd934x_wcd_mbhc_qfuse_cal(component, &z1Ls, 0);
	/* Parallel of left Z and 9 ohm pull down resistor */
	zMono = ((*zl) * 9) / ((*zl) + 9);
	z_diff1 = (z1Ls > zMono) ? (z1Ls - zMono) : (zMono - z1Ls);
	z_diff2 = ((*zl) > z1Ls) ? ((*zl) - z1Ls) : (z1Ls - (*zl));
	if ((z_diff1 * (*zl + z1Ls)) > (z_diff2 * (z1Ls + zMono))) {
		dev_err(component->dev, "%s: stereo plug type detected\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd934x->mbhc, WCD_MBHC_HPH_STEREO);
	} else {
		dev_err(component->dev, "%s: MONO plug type detected\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd934x->mbhc, WCD_MBHC_HPH_MONO);
	}

zdet_complete:
	snd_soc_component_write(component, WCD934X_ANA_MBHC_BTN5, reg0);
	snd_soc_component_write(component, WCD934X_ANA_MBHC_BTN6, reg1);
	snd_soc_component_write(component, WCD934X_ANA_MBHC_BTN7, reg2);
	/* Turn on 100k pull down on HPHL */
	regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_MECH, 0x01, 0x01);

	/* For NO-jack, re-enable L_DET_EN after Z-det measurements */
	if (wcd934x->mbhc_cfg.hphl_swh)
		regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_MECH, 0x80, 0x80);

	snd_soc_component_write(component, WCD934X_MBHC_NEW_ZDET_ANA_CTL, reg4);
	snd_soc_component_write(component, WCD934X_MBHC_CTL_CLK, reg3);
	if (is_fsm_disable)
		regmap_update_bits(wcd934x->regmap, WCD934X_ANA_MBHC_ELECT, 0x80, 0x80);
}

static void wcd934x_mbhc_gnd_det_ctrl(struct snd_soc_component *component,
			bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD934X_ANA_MBHC_MECH,
					      WCD934X_MBHC_HSG_PULLUP_COMP_EN, 1);
		snd_soc_component_write_field(component, WCD934X_ANA_MBHC_MECH,
					      WCD934X_MBHC_GND_DET_EN_MASK, 1);
	} else {
		snd_soc_component_write_field(component, WCD934X_ANA_MBHC_MECH,
					      WCD934X_MBHC_GND_DET_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD934X_ANA_MBHC_MECH,
					      WCD934X_MBHC_HSG_PULLUP_COMP_EN, 0);
	}
}

static void wcd934x_mbhc_hph_pull_down_ctrl(struct snd_soc_component *component,
					  bool enable)
{
	snd_soc_component_write_field(component, WCD934X_HPH_PA_CTL2,
				      WCD934X_HPHPA_GND_R_MASK, enable);
	snd_soc_component_write_field(component, WCD934X_HPH_PA_CTL2,
				      WCD934X_HPHPA_GND_L_MASK, enable);
}

static const struct wcd_mbhc_cb mbhc_cb = {
	.clk_setup = wcd934x_mbhc_clk_setup,
	.mbhc_bias = wcd934x_mbhc_mbhc_bias_control,
	.set_btn_thr = wcd934x_mbhc_program_btn_thr,
	.micbias_enable_status = wcd934x_mbhc_micb_en_status,
	.hph_pull_up_control = wcd934x_mbhc_hph_l_pull_up_control,
	.mbhc_micbias_control = wcd934x_mbhc_request_micbias,
	.mbhc_micb_ramp_control = wcd934x_mbhc_micb_ramp_control,
	.mbhc_micb_ctrl_thr_mic = wcd934x_mbhc_micb_ctrl_threshold_mic,
	.compute_impedance = wcd934x_wcd_mbhc_calc_impedance,
	.mbhc_gnd_det_ctrl = wcd934x_mbhc_gnd_det_ctrl,
	.hph_pull_down_ctrl = wcd934x_mbhc_hph_pull_down_ctrl,
};

static int wcd934x_get_hph_type(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd_mbhc_get_hph_type(wcd->mbhc);

	return 0;
}

static int wcd934x_hph_impedance_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	uint32_t zl, zr;
	bool hphr;
	struct soc_mixer_control *mc;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(component);

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;
	wcd_mbhc_get_impedance(wcd->mbhc, &zl, &zr);
	dev_dbg(component->dev, "%s: zl=%u(ohms), zr=%u(ohms)\n", __func__, zl, zr);
	ucontrol->value.integer.value[0] = hphr ? zr : zl;

	return 0;
}
static const struct snd_kcontrol_new hph_type_detect_controls[] = {
	SOC_SINGLE_EXT("HPH Type", 0, 0, WCD_MBHC_HPH_STEREO, 0,
		       wcd934x_get_hph_type, NULL),
};

static const struct snd_kcontrol_new impedance_detect_controls[] = {
	SOC_SINGLE_EXT("HPHL Impedance", 0, 0, INT_MAX, 0,
		       wcd934x_hph_impedance_get, NULL),
	SOC_SINGLE_EXT("HPHR Impedance", 0, 1, INT_MAX, 0,
		       wcd934x_hph_impedance_get, NULL),
};

static int wcd934x_mbhc_init(struct snd_soc_component *component)
{
	struct wcd934x_ddata *data = dev_get_drvdata(component->dev->parent);
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(component);
	struct wcd_mbhc_intr *intr_ids = &wcd->intr_ids;

	intr_ids->mbhc_sw_intr = regmap_irq_get_virq(data->irq_data,
						     WCD934X_IRQ_MBHC_SW_DET);
	intr_ids->mbhc_btn_press_intr = regmap_irq_get_virq(data->irq_data,
							    WCD934X_IRQ_MBHC_BUTTON_PRESS_DET);
	intr_ids->mbhc_btn_release_intr = regmap_irq_get_virq(data->irq_data,
							      WCD934X_IRQ_MBHC_BUTTON_RELEASE_DET);
	intr_ids->mbhc_hs_ins_intr = regmap_irq_get_virq(data->irq_data,
							 WCD934X_IRQ_MBHC_ELECT_INS_REM_LEG_DET);
	intr_ids->mbhc_hs_rem_intr = regmap_irq_get_virq(data->irq_data,
							 WCD934X_IRQ_MBHC_ELECT_INS_REM_DET);
	intr_ids->hph_left_ocp = regmap_irq_get_virq(data->irq_data,
						     WCD934X_IRQ_HPH_PA_OCPL_FAULT);
	intr_ids->hph_right_ocp = regmap_irq_get_virq(data->irq_data,
						      WCD934X_IRQ_HPH_PA_OCPR_FAULT);

	wcd->mbhc = wcd_mbhc_init(component, &mbhc_cb, intr_ids, wcd_mbhc_fields, true);
	if (IS_ERR(wcd->mbhc)) {
		wcd->mbhc = NULL;
		return -EINVAL;
	}

	snd_soc_add_component_controls(component, impedance_detect_controls,
				       ARRAY_SIZE(impedance_detect_controls));
	snd_soc_add_component_controls(component, hph_type_detect_controls,
				       ARRAY_SIZE(hph_type_detect_controls));

	return 0;
}

static void wcd934x_mbhc_deinit(struct snd_soc_component *component)
{
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(component);

	if (!wcd->mbhc)
		return;

	wcd_mbhc_deinit(wcd->mbhc);
}

static int wcd934x_comp_probe(struct snd_soc_component *component)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);
	int i;

	snd_soc_component_init_regmap(component, wcd->regmap);
	wcd->component = component;

	/* Class-H Init*/
	wcd->clsh_ctrl = wcd_clsh_ctrl_alloc(component, wcd->version);
	if (IS_ERR(wcd->clsh_ctrl))
		return PTR_ERR(wcd->clsh_ctrl);

	/* Default HPH Mode to Class-H Low HiFi */
	wcd->hph_mode = CLS_H_LOHIFI;

	wcd934x_comp_init(component);

	for (i = 0; i < NUM_CODEC_DAIS; i++)
		INIT_LIST_HEAD(&wcd->dai[i].slim_ch_list);

	wcd934x_init_dmic(component);

	if (wcd934x_mbhc_init(component))
		dev_err(component->dev, "Failed to Initialize MBHC\n");

	return 0;
}

static void wcd934x_comp_remove(struct snd_soc_component *comp)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);

	wcd934x_mbhc_deinit(comp);
	wcd_clsh_ctrl_free(wcd->clsh_ctrl);
}

static int wcd934x_comp_set_sysclk(struct snd_soc_component *comp,
				   int clk_id, int source,
				   unsigned int freq, int dir)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	int val = WCD934X_CODEC_RPM_CLK_MCLK_CFG_9P6MHZ;

	wcd->rate = freq;

	if (wcd->rate == WCD934X_MCLK_CLK_12P288MHZ)
		val = WCD934X_CODEC_RPM_CLK_MCLK_CFG_12P288MHZ;

	snd_soc_component_update_bits(comp, WCD934X_CODEC_RPM_CLK_MCLK_CFG,
				      WCD934X_CODEC_RPM_CLK_MCLK_CFG_MCLK_MASK,
				      val);

	return clk_set_rate(wcd->extclk, freq);
}

static uint32_t get_iir_band_coeff(struct snd_soc_component *component,
				   int iir_idx, int band_idx, int coeff_idx)
{
	u32 value = 0;
	int reg, b2_reg;

	/* Address does not automatically update if reading */
	reg = WCD934X_CDC_SIDETONE_IIR0_IIR_COEF_B1_CTL + 16 * iir_idx;
	b2_reg = WCD934X_CDC_SIDETONE_IIR0_IIR_COEF_B2_CTL + 16 * iir_idx;

	snd_soc_component_write(component, reg,
				((band_idx * BAND_MAX + coeff_idx) *
				 sizeof(uint32_t)) & 0x7F);

	value |= snd_soc_component_read(component, b2_reg);
	snd_soc_component_write(component, reg,
				((band_idx * BAND_MAX + coeff_idx)
				 * sizeof(uint32_t) + 1) & 0x7F);

	value |= (snd_soc_component_read(component, b2_reg) << 8);
	snd_soc_component_write(component, reg,
				((band_idx * BAND_MAX + coeff_idx)
				 * sizeof(uint32_t) + 2) & 0x7F);

	value |= (snd_soc_component_read(component, b2_reg) << 16);
	snd_soc_component_write(component, reg,
		((band_idx * BAND_MAX + coeff_idx)
		* sizeof(uint32_t) + 3) & 0x7F);

	/* Mask bits top 2 bits since they are reserved */
	value |= (snd_soc_component_read(component, b2_reg) << 24);
	return value;
}

static void set_iir_band_coeff(struct snd_soc_component *component,
			       int iir_idx, int band_idx, uint32_t value)
{
	int reg = WCD934X_CDC_SIDETONE_IIR0_IIR_COEF_B2_CTL + 16 * iir_idx;

	snd_soc_component_write(component, reg, (value & 0xFF));
	snd_soc_component_write(component, reg, (value >> 8) & 0xFF);
	snd_soc_component_write(component, reg, (value >> 16) & 0xFF);
	/* Mask top 2 bits, 7-8 are reserved */
	snd_soc_component_write(component, reg, (value >> 24) & 0x3F);
}

static int wcd934x_put_iir_band_audio_mixer(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct wcd_iir_filter_ctl *ctl =
			(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;
	int iir_idx = ctl->iir_idx;
	int band_idx = ctl->band_idx;
	u32 coeff[BAND_MAX];
	int reg = WCD934X_CDC_SIDETONE_IIR0_IIR_COEF_B1_CTL + 16 * iir_idx;

	memcpy(&coeff[0], ucontrol->value.bytes.data, params->max);

	/* Mask top bit it is reserved */
	/* Updates addr automatically for each B2 write */
	snd_soc_component_write(component, reg, (band_idx * BAND_MAX *
						 sizeof(uint32_t)) & 0x7F);

	set_iir_band_coeff(component, iir_idx, band_idx, coeff[0]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[1]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[2]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[3]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[4]);

	return 0;
}

static int wcd934x_get_iir_band_audio_mixer(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct wcd_iir_filter_ctl *ctl =
			(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;
	int iir_idx = ctl->iir_idx;
	int band_idx = ctl->band_idx;
	u32 coeff[BAND_MAX];

	coeff[0] = get_iir_band_coeff(component, iir_idx, band_idx, 0);
	coeff[1] = get_iir_band_coeff(component, iir_idx, band_idx, 1);
	coeff[2] = get_iir_band_coeff(component, iir_idx, band_idx, 2);
	coeff[3] = get_iir_band_coeff(component, iir_idx, band_idx, 3);
	coeff[4] = get_iir_band_coeff(component, iir_idx, band_idx, 4);

	memcpy(ucontrol->value.bytes.data, &coeff[0], params->max);

	return 0;
}

static int wcd934x_iir_filter_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *ucontrol)
{
	struct wcd_iir_filter_ctl *ctl =
		(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;

	ucontrol->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	ucontrol->count = params->max;

	return 0;
}

static int wcd934x_compander_get(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	int comp = ((struct soc_mixer_control *)kc->private_value)->shift;
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);

	ucontrol->value.integer.value[0] = wcd->comp_enabled[comp];

	return 0;
}

static int wcd934x_compander_set(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);
	int comp = ((struct soc_mixer_control *)kc->private_value)->shift;
	int value = ucontrol->value.integer.value[0];
	int sel;

	if (wcd->comp_enabled[comp] == value)
		return 0;

	wcd->comp_enabled[comp] = value;
	sel = value ? WCD934X_HPH_GAIN_SRC_SEL_COMPANDER :
		WCD934X_HPH_GAIN_SRC_SEL_REGISTER;

	/* Any specific register configuration for compander */
	switch (comp) {
	case COMPANDER_1:
		/* Set Gain Source Select based on compander enable/disable */
		snd_soc_component_update_bits(component, WCD934X_HPH_L_EN,
					      WCD934X_HPH_GAIN_SRC_SEL_MASK,
					      sel);
		break;
	case COMPANDER_2:
		snd_soc_component_update_bits(component, WCD934X_HPH_R_EN,
					      WCD934X_HPH_GAIN_SRC_SEL_MASK,
					      sel);
		break;
	case COMPANDER_3:
	case COMPANDER_4:
	case COMPANDER_7:
	case COMPANDER_8:
		break;
	default:
		return 0;
	}

	return 1;
}

static int wcd934x_rx_hph_mode_get(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);

	ucontrol->value.enumerated.item[0] = wcd->hph_mode;

	return 0;
}

static int wcd934x_rx_hph_mode_put(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);
	u32 mode_val;

	mode_val = ucontrol->value.enumerated.item[0];

	if (mode_val == wcd->hph_mode)
		return 0;

	if (mode_val == 0) {
		dev_err(wcd->dev, "Invalid HPH Mode, default to ClSH HiFi\n");
		mode_val = CLS_H_LOHIFI;
	}
	wcd->hph_mode = mode_val;

	return 1;
}

static int slim_rx_mux_get(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kc);
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kc);
	struct wcd934x_codec *wcd = dev_get_drvdata(dapm->dev);

	ucontrol->value.enumerated.item[0] = wcd->rx_port_value[w->shift];

	return 0;
}

static int slim_rx_mux_to_dai_id(int mux)
{
	int aif_id;

	switch (mux) {
	case 1:
		aif_id = AIF1_PB;
		break;
	case 2:
		aif_id = AIF2_PB;
		break;
	case 3:
		aif_id = AIF3_PB;
		break;
	case 4:
		aif_id = AIF4_PB;
		break;
	default:
		aif_id = -1;
		break;
	}

	return aif_id;
}

static int slim_rx_mux_put(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kc);
	struct wcd934x_codec *wcd = dev_get_drvdata(w->dapm->dev);
	struct soc_enum *e = (struct soc_enum *)kc->private_value;
	struct snd_soc_dapm_update *update = NULL;
	struct wcd934x_slim_ch *ch, *c;
	u32 port_id = w->shift;
	bool found = false;
	int mux_idx;
	int prev_mux_idx = wcd->rx_port_value[port_id];
	int aif_id;

	mux_idx = ucontrol->value.enumerated.item[0];

	if (mux_idx == prev_mux_idx)
		return 0;

	switch(mux_idx) {
	case 0:
		aif_id = slim_rx_mux_to_dai_id(prev_mux_idx);
		if (aif_id < 0)
			return 0;

		list_for_each_entry_safe(ch, c, &wcd->dai[aif_id].slim_ch_list, list) {
			if (ch->port == port_id + WCD934X_RX_START) {
				found = true;
				list_del_init(&ch->list);
				break;
			}
		}
		if (!found)
			return 0;

		break;
	case 1 ... 4:
		aif_id = slim_rx_mux_to_dai_id(mux_idx);
		if (aif_id < 0)
			return 0;

		if (list_empty(&wcd->rx_chs[port_id].list)) {
			list_add_tail(&wcd->rx_chs[port_id].list,
				      &wcd->dai[aif_id].slim_ch_list);
		} else {
			dev_err(wcd->dev ,"SLIM_RX%d PORT is busy\n", port_id);
			return 0;
		}
		break;

	default:
		dev_err(wcd->dev, "Unknown AIF %d\n", mux_idx);
		goto err;
	}

	wcd->rx_port_value[port_id] = mux_idx;
	snd_soc_dapm_mux_update_power(w->dapm, kc, wcd->rx_port_value[port_id],
				      e, update);

	return 1;
err:
	return -EINVAL;
}

static int wcd934x_int_dem_inp_mux_put(struct snd_kcontrol *kc,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *)kc->private_value;
	struct snd_soc_component *component;
	int reg, val;

	component = snd_soc_dapm_kcontrol_component(kc);
	val = ucontrol->value.enumerated.item[0];
	if (e->reg == WCD934X_CDC_RX0_RX_PATH_SEC0)
		reg = WCD934X_CDC_RX0_RX_PATH_CFG0;
	else if (e->reg == WCD934X_CDC_RX1_RX_PATH_SEC0)
		reg = WCD934X_CDC_RX1_RX_PATH_CFG0;
	else if (e->reg == WCD934X_CDC_RX2_RX_PATH_SEC0)
		reg = WCD934X_CDC_RX2_RX_PATH_CFG0;
	else
		return -EINVAL;

	/* Set Look Ahead Delay */
	if (val)
		snd_soc_component_update_bits(component, reg,
					      WCD934X_RX_DLY_ZN_EN_MASK,
					      WCD934X_RX_DLY_ZN_ENABLE);
	else
		snd_soc_component_update_bits(component, reg,
					      WCD934X_RX_DLY_ZN_EN_MASK,
					      WCD934X_RX_DLY_ZN_DISABLE);

	return snd_soc_dapm_put_enum_double(kc, ucontrol);
}

static int wcd934x_dec_enum_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val;
	u16 mic_sel_reg = 0;
	u8 mic_sel;

	comp = snd_soc_dapm_kcontrol_component(kcontrol);

	val = ucontrol->value.enumerated.item[0];
	if (val > e->items - 1)
		return -EINVAL;

	switch (e->reg) {
	case WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG1:
		if (e->shift_l == 0)
			mic_sel_reg = WCD934X_CDC_TX0_TX_PATH_CFG0;
		else if (e->shift_l == 2)
			mic_sel_reg = WCD934X_CDC_TX4_TX_PATH_CFG0;
		else if (e->shift_l == 4)
			mic_sel_reg = WCD934X_CDC_TX8_TX_PATH_CFG0;
		break;
	case WCD934X_CDC_TX_INP_MUX_ADC_MUX1_CFG1:
		if (e->shift_l == 0)
			mic_sel_reg = WCD934X_CDC_TX1_TX_PATH_CFG0;
		else if (e->shift_l == 2)
			mic_sel_reg = WCD934X_CDC_TX5_TX_PATH_CFG0;
		break;
	case WCD934X_CDC_TX_INP_MUX_ADC_MUX2_CFG1:
		if (e->shift_l == 0)
			mic_sel_reg = WCD934X_CDC_TX2_TX_PATH_CFG0;
		else if (e->shift_l == 2)
			mic_sel_reg = WCD934X_CDC_TX6_TX_PATH_CFG0;
		break;
	case WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG1:
		if (e->shift_l == 0)
			mic_sel_reg = WCD934X_CDC_TX3_TX_PATH_CFG0;
		else if (e->shift_l == 2)
			mic_sel_reg = WCD934X_CDC_TX7_TX_PATH_CFG0;
		break;
	default:
		dev_err(comp->dev, "%s: e->reg: 0x%x not expected\n",
			__func__, e->reg);
		return -EINVAL;
	}

	/* ADC: 0, DMIC: 1 */
	mic_sel = val ? 0x0 : 0x1;
	if (mic_sel_reg)
		snd_soc_component_update_bits(comp, mic_sel_reg, BIT(7),
					      mic_sel << 7);

	return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
}

static const struct snd_kcontrol_new rx_int0_2_mux =
	SOC_DAPM_ENUM("RX INT0_2 MUX Mux", rx_int0_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int1_2_mux =
	SOC_DAPM_ENUM("RX INT1_2 MUX Mux", rx_int1_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int2_2_mux =
	SOC_DAPM_ENUM("RX INT2_2 MUX Mux", rx_int2_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int3_2_mux =
	SOC_DAPM_ENUM("RX INT3_2 MUX Mux", rx_int3_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int4_2_mux =
	SOC_DAPM_ENUM("RX INT4_2 MUX Mux", rx_int4_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int7_2_mux =
	SOC_DAPM_ENUM("RX INT7_2 MUX Mux", rx_int7_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int8_2_mux =
	SOC_DAPM_ENUM("RX INT8_2 MUX Mux", rx_int8_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int0_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT0_1 MIX1 INP0 Mux", rx_int0_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int0_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT0_1 MIX1 INP1 Mux", rx_int0_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int0_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT0_1 MIX1 INP2 Mux", rx_int0_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int1_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT1_1 MIX1 INP0 Mux", rx_int1_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int1_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT1_1 MIX1 INP1 Mux", rx_int1_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int1_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT1_1 MIX1 INP2 Mux", rx_int1_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int2_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT2_1 MIX1 INP0 Mux", rx_int2_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int2_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT2_1 MIX1 INP1 Mux", rx_int2_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int2_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT2_1 MIX1 INP2 Mux", rx_int2_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int3_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT3_1 MIX1 INP0 Mux", rx_int3_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int3_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT3_1 MIX1 INP1 Mux", rx_int3_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int3_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT3_1 MIX1 INP2 Mux", rx_int3_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int4_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT4_1 MIX1 INP0 Mux", rx_int4_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int4_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT4_1 MIX1 INP1 Mux", rx_int4_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int4_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT4_1 MIX1 INP2 Mux", rx_int4_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int7_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT7_1 MIX1 INP0 Mux", rx_int7_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int7_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT7_1 MIX1 INP1 Mux", rx_int7_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int7_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT7_1 MIX1 INP2 Mux", rx_int7_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int8_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT8_1 MIX1 INP0 Mux", rx_int8_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int8_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT8_1 MIX1 INP1 Mux", rx_int8_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int8_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT8_1 MIX1 INP2 Mux", rx_int8_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int0_mix2_inp_mux =
	SOC_DAPM_ENUM("RX INT0 MIX2 INP Mux", rx_int0_mix2_inp_mux_enum);

static const struct snd_kcontrol_new rx_int1_mix2_inp_mux =
	SOC_DAPM_ENUM("RX INT1 MIX2 INP Mux", rx_int1_mix2_inp_mux_enum);

static const struct snd_kcontrol_new rx_int2_mix2_inp_mux =
	SOC_DAPM_ENUM("RX INT2 MIX2 INP Mux", rx_int2_mix2_inp_mux_enum);

static const struct snd_kcontrol_new rx_int3_mix2_inp_mux =
	SOC_DAPM_ENUM("RX INT3 MIX2 INP Mux", rx_int3_mix2_inp_mux_enum);

static const struct snd_kcontrol_new rx_int4_mix2_inp_mux =
	SOC_DAPM_ENUM("RX INT4 MIX2 INP Mux", rx_int4_mix2_inp_mux_enum);

static const struct snd_kcontrol_new rx_int7_mix2_inp_mux =
	SOC_DAPM_ENUM("RX INT7 MIX2 INP Mux", rx_int7_mix2_inp_mux_enum);

static const struct snd_kcontrol_new iir0_inp0_mux =
	SOC_DAPM_ENUM("IIR0 INP0 Mux", iir0_inp0_mux_enum);
static const struct snd_kcontrol_new iir0_inp1_mux =
	SOC_DAPM_ENUM("IIR0 INP1 Mux", iir0_inp1_mux_enum);
static const struct snd_kcontrol_new iir0_inp2_mux =
	SOC_DAPM_ENUM("IIR0 INP2 Mux", iir0_inp2_mux_enum);
static const struct snd_kcontrol_new iir0_inp3_mux =
	SOC_DAPM_ENUM("IIR0 INP3 Mux", iir0_inp3_mux_enum);

static const struct snd_kcontrol_new iir1_inp0_mux =
	SOC_DAPM_ENUM("IIR1 INP0 Mux", iir1_inp0_mux_enum);
static const struct snd_kcontrol_new iir1_inp1_mux =
	SOC_DAPM_ENUM("IIR1 INP1 Mux", iir1_inp1_mux_enum);
static const struct snd_kcontrol_new iir1_inp2_mux =
	SOC_DAPM_ENUM("IIR1 INP2 Mux", iir1_inp2_mux_enum);
static const struct snd_kcontrol_new iir1_inp3_mux =
	SOC_DAPM_ENUM("IIR1 INP3 Mux", iir1_inp3_mux_enum);

static const struct snd_kcontrol_new slim_rx_mux[WCD934X_RX_MAX] = {
	SOC_DAPM_ENUM_EXT("SLIM RX0 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
	SOC_DAPM_ENUM_EXT("SLIM RX1 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
	SOC_DAPM_ENUM_EXT("SLIM RX2 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
	SOC_DAPM_ENUM_EXT("SLIM RX3 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
	SOC_DAPM_ENUM_EXT("SLIM RX4 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
	SOC_DAPM_ENUM_EXT("SLIM RX5 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
	SOC_DAPM_ENUM_EXT("SLIM RX6 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
	SOC_DAPM_ENUM_EXT("SLIM RX7 Mux", slim_rx_mux_enum,
			  slim_rx_mux_get, slim_rx_mux_put),
};

static const struct snd_kcontrol_new rx_int1_asrc_switch[] = {
	SOC_DAPM_SINGLE("HPHL Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct snd_kcontrol_new rx_int2_asrc_switch[] = {
	SOC_DAPM_SINGLE("HPHR Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct snd_kcontrol_new rx_int3_asrc_switch[] = {
	SOC_DAPM_SINGLE("LO1 Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct snd_kcontrol_new rx_int4_asrc_switch[] = {
	SOC_DAPM_SINGLE("LO2 Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct snd_kcontrol_new rx_int0_dem_inp_mux =
	SOC_DAPM_ENUM_EXT("RX INT0 DEM MUX Mux", rx_int0_dem_inp_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  wcd934x_int_dem_inp_mux_put);

static const struct snd_kcontrol_new rx_int1_dem_inp_mux =
	SOC_DAPM_ENUM_EXT("RX INT1 DEM MUX Mux", rx_int1_dem_inp_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  wcd934x_int_dem_inp_mux_put);

static const struct snd_kcontrol_new rx_int2_dem_inp_mux =
	SOC_DAPM_ENUM_EXT("RX INT2 DEM MUX Mux", rx_int2_dem_inp_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  wcd934x_int_dem_inp_mux_put);

static const struct snd_kcontrol_new rx_int0_1_interp_mux =
	SOC_DAPM_ENUM("RX INT0_1 INTERP Mux", rx_int0_1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int1_1_interp_mux =
	SOC_DAPM_ENUM("RX INT1_1 INTERP Mux", rx_int1_1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int2_1_interp_mux =
	SOC_DAPM_ENUM("RX INT2_1 INTERP Mux", rx_int2_1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int3_1_interp_mux =
	SOC_DAPM_ENUM("RX INT3_1 INTERP Mux", rx_int3_1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int4_1_interp_mux =
	SOC_DAPM_ENUM("RX INT4_1 INTERP Mux", rx_int4_1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int7_1_interp_mux =
	SOC_DAPM_ENUM("RX INT7_1 INTERP Mux", rx_int7_1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int8_1_interp_mux =
	SOC_DAPM_ENUM("RX INT8_1 INTERP Mux", rx_int8_1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int0_2_interp_mux =
	SOC_DAPM_ENUM("RX INT0_2 INTERP Mux", rx_int0_2_interp_mux_enum);

static const struct snd_kcontrol_new rx_int1_2_interp_mux =
	SOC_DAPM_ENUM("RX INT1_2 INTERP Mux", rx_int1_2_interp_mux_enum);

static const struct snd_kcontrol_new rx_int2_2_interp_mux =
	SOC_DAPM_ENUM("RX INT2_2 INTERP Mux", rx_int2_2_interp_mux_enum);

static const struct snd_kcontrol_new rx_int3_2_interp_mux =
	SOC_DAPM_ENUM("RX INT3_2 INTERP Mux", rx_int3_2_interp_mux_enum);

static const struct snd_kcontrol_new rx_int4_2_interp_mux =
	SOC_DAPM_ENUM("RX INT4_2 INTERP Mux", rx_int4_2_interp_mux_enum);

static const struct snd_kcontrol_new rx_int7_2_interp_mux =
	SOC_DAPM_ENUM("RX INT7_2 INTERP Mux", rx_int7_2_interp_mux_enum);

static const struct snd_kcontrol_new rx_int8_2_interp_mux =
	SOC_DAPM_ENUM("RX INT8_2 INTERP Mux", rx_int8_2_interp_mux_enum);

static const struct snd_kcontrol_new tx_dmic_mux0 =
	SOC_DAPM_ENUM("DMIC MUX0 Mux", tx_dmic_mux0_enum);

static const struct snd_kcontrol_new tx_dmic_mux1 =
	SOC_DAPM_ENUM("DMIC MUX1 Mux", tx_dmic_mux1_enum);

static const struct snd_kcontrol_new tx_dmic_mux2 =
	SOC_DAPM_ENUM("DMIC MUX2 Mux", tx_dmic_mux2_enum);

static const struct snd_kcontrol_new tx_dmic_mux3 =
	SOC_DAPM_ENUM("DMIC MUX3 Mux", tx_dmic_mux3_enum);

static const struct snd_kcontrol_new tx_dmic_mux4 =
	SOC_DAPM_ENUM("DMIC MUX4 Mux", tx_dmic_mux4_enum);

static const struct snd_kcontrol_new tx_dmic_mux5 =
	SOC_DAPM_ENUM("DMIC MUX5 Mux", tx_dmic_mux5_enum);

static const struct snd_kcontrol_new tx_dmic_mux6 =
	SOC_DAPM_ENUM("DMIC MUX6 Mux", tx_dmic_mux6_enum);

static const struct snd_kcontrol_new tx_dmic_mux7 =
	SOC_DAPM_ENUM("DMIC MUX7 Mux", tx_dmic_mux7_enum);

static const struct snd_kcontrol_new tx_dmic_mux8 =
	SOC_DAPM_ENUM("DMIC MUX8 Mux", tx_dmic_mux8_enum);

static const struct snd_kcontrol_new tx_amic_mux0 =
	SOC_DAPM_ENUM("AMIC MUX0 Mux", tx_amic_mux0_enum);

static const struct snd_kcontrol_new tx_amic_mux1 =
	SOC_DAPM_ENUM("AMIC MUX1 Mux", tx_amic_mux1_enum);

static const struct snd_kcontrol_new tx_amic_mux2 =
	SOC_DAPM_ENUM("AMIC MUX2 Mux", tx_amic_mux2_enum);

static const struct snd_kcontrol_new tx_amic_mux3 =
	SOC_DAPM_ENUM("AMIC MUX3 Mux", tx_amic_mux3_enum);

static const struct snd_kcontrol_new tx_amic_mux4 =
	SOC_DAPM_ENUM("AMIC MUX4 Mux", tx_amic_mux4_enum);

static const struct snd_kcontrol_new tx_amic_mux5 =
	SOC_DAPM_ENUM("AMIC MUX5 Mux", tx_amic_mux5_enum);

static const struct snd_kcontrol_new tx_amic_mux6 =
	SOC_DAPM_ENUM("AMIC MUX6 Mux", tx_amic_mux6_enum);

static const struct snd_kcontrol_new tx_amic_mux7 =
	SOC_DAPM_ENUM("AMIC MUX7 Mux", tx_amic_mux7_enum);

static const struct snd_kcontrol_new tx_amic_mux8 =
	SOC_DAPM_ENUM("AMIC MUX8 Mux", tx_amic_mux8_enum);

static const struct snd_kcontrol_new tx_amic4_5 =
	SOC_DAPM_ENUM("AMIC4_5 SEL Mux", tx_amic4_5_enum);

static const struct snd_kcontrol_new tx_adc_mux0_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX0 Mux", tx_adc_mux0_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux1_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX1 Mux", tx_adc_mux1_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux2_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX2 Mux", tx_adc_mux2_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux3_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX3 Mux", tx_adc_mux3_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux4_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX4 Mux", tx_adc_mux4_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux5_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX5 Mux", tx_adc_mux5_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux6_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX6 Mux", tx_adc_mux6_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux7_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX7 Mux", tx_adc_mux7_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);
static const struct snd_kcontrol_new tx_adc_mux8_mux =
	SOC_DAPM_ENUM_EXT("ADC MUX8 Mux", tx_adc_mux8_enum,
			  snd_soc_dapm_get_enum_double, wcd934x_dec_enum_put);

static const struct snd_kcontrol_new cdc_if_tx0_mux =
	SOC_DAPM_ENUM("CDC_IF TX0 MUX Mux", cdc_if_tx0_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx1_mux =
	SOC_DAPM_ENUM("CDC_IF TX1 MUX Mux", cdc_if_tx1_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx2_mux =
	SOC_DAPM_ENUM("CDC_IF TX2 MUX Mux", cdc_if_tx2_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx3_mux =
	SOC_DAPM_ENUM("CDC_IF TX3 MUX Mux", cdc_if_tx3_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx4_mux =
	SOC_DAPM_ENUM("CDC_IF TX4 MUX Mux", cdc_if_tx4_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx5_mux =
	SOC_DAPM_ENUM("CDC_IF TX5 MUX Mux", cdc_if_tx5_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx6_mux =
	SOC_DAPM_ENUM("CDC_IF TX6 MUX Mux", cdc_if_tx6_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx7_mux =
	SOC_DAPM_ENUM("CDC_IF TX7 MUX Mux", cdc_if_tx7_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx8_mux =
	SOC_DAPM_ENUM("CDC_IF TX8 MUX Mux", cdc_if_tx8_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx9_mux =
	SOC_DAPM_ENUM("CDC_IF TX9 MUX Mux", cdc_if_tx9_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx10_mux =
	SOC_DAPM_ENUM("CDC_IF TX10 MUX Mux", cdc_if_tx10_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx11_mux =
	SOC_DAPM_ENUM("CDC_IF TX11 MUX Mux", cdc_if_tx11_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx11_inp1_mux =
	SOC_DAPM_ENUM("CDC_IF TX11 INP1 MUX Mux", cdc_if_tx11_inp1_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx13_mux =
	SOC_DAPM_ENUM("CDC_IF TX13 MUX Mux", cdc_if_tx13_mux_enum);
static const struct snd_kcontrol_new cdc_if_tx13_inp1_mux =
	SOC_DAPM_ENUM("CDC_IF TX13 INP1 MUX Mux", cdc_if_tx13_inp1_mux_enum);

static int slim_tx_mixer_get(struct snd_kcontrol *kc,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kc);
	struct wcd934x_codec *wcd = dev_get_drvdata(dapm->dev);
	struct soc_mixer_control *mixer =
			(struct soc_mixer_control *)kc->private_value;
	int port_id = mixer->shift;

	ucontrol->value.integer.value[0] = wcd->tx_port_value[port_id];

	return 0;
}

static int slim_tx_mixer_put(struct snd_kcontrol *kc,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kc);
	struct wcd934x_codec *wcd = dev_get_drvdata(widget->dapm->dev);
	struct snd_soc_dapm_update *update = NULL;
	struct soc_mixer_control *mixer =
			(struct soc_mixer_control *)kc->private_value;
	int enable = ucontrol->value.integer.value[0];
	struct wcd934x_slim_ch *ch, *c;
	int dai_id = widget->shift;
	int port_id = mixer->shift;

	/* only add to the list if value not set */
	if (enable == wcd->tx_port_value[port_id])
		return 0;

	if (enable) {
		if (list_empty(&wcd->tx_chs[port_id].list)) {
			list_add_tail(&wcd->tx_chs[port_id].list,
				      &wcd->dai[dai_id].slim_ch_list);
		} else {
			dev_err(wcd->dev ,"SLIM_TX%d PORT is busy\n", port_id);
			return 0;
		}
	 } else {
		bool found = false;

		list_for_each_entry_safe(ch, c, &wcd->dai[dai_id].slim_ch_list, list) {
			if (ch->port == port_id) {
				found = true;
				list_del_init(&wcd->tx_chs[port_id].list);
				break;
			}
		}
		if (!found)
			return 0;
	 }

	wcd->tx_port_value[port_id] = enable;
	snd_soc_dapm_mixer_update_power(widget->dapm, kc, enable, update);

	return 1;
}

static const struct snd_kcontrol_new aif1_slim_cap_mixer[] = {
	SOC_SINGLE_EXT("SLIM TX0", SND_SOC_NOPM, WCD934X_TX0, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX1", SND_SOC_NOPM, WCD934X_TX1, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX2", SND_SOC_NOPM, WCD934X_TX2, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX3", SND_SOC_NOPM, WCD934X_TX3, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX4", SND_SOC_NOPM, WCD934X_TX4, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX5", SND_SOC_NOPM, WCD934X_TX5, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX6", SND_SOC_NOPM, WCD934X_TX6, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX7", SND_SOC_NOPM, WCD934X_TX7, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX8", SND_SOC_NOPM, WCD934X_TX8, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX9", SND_SOC_NOPM, WCD934X_TX9, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX10", SND_SOC_NOPM, WCD934X_TX10, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX11", SND_SOC_NOPM, WCD934X_TX11, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX13", SND_SOC_NOPM, WCD934X_TX13, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
};

static const struct snd_kcontrol_new aif2_slim_cap_mixer[] = {
	SOC_SINGLE_EXT("SLIM TX0", SND_SOC_NOPM, WCD934X_TX0, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX1", SND_SOC_NOPM, WCD934X_TX1, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX2", SND_SOC_NOPM, WCD934X_TX2, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX3", SND_SOC_NOPM, WCD934X_TX3, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX4", SND_SOC_NOPM, WCD934X_TX4, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX5", SND_SOC_NOPM, WCD934X_TX5, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX6", SND_SOC_NOPM, WCD934X_TX6, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX7", SND_SOC_NOPM, WCD934X_TX7, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX8", SND_SOC_NOPM, WCD934X_TX8, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX9", SND_SOC_NOPM, WCD934X_TX9, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX10", SND_SOC_NOPM, WCD934X_TX10, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX11", SND_SOC_NOPM, WCD934X_TX11, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX13", SND_SOC_NOPM, WCD934X_TX13, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
};

static const struct snd_kcontrol_new aif3_slim_cap_mixer[] = {
	SOC_SINGLE_EXT("SLIM TX0", SND_SOC_NOPM, WCD934X_TX0, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX1", SND_SOC_NOPM, WCD934X_TX1, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX2", SND_SOC_NOPM, WCD934X_TX2, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX3", SND_SOC_NOPM, WCD934X_TX3, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX4", SND_SOC_NOPM, WCD934X_TX4, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX5", SND_SOC_NOPM, WCD934X_TX5, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX6", SND_SOC_NOPM, WCD934X_TX6, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX7", SND_SOC_NOPM, WCD934X_TX7, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX8", SND_SOC_NOPM, WCD934X_TX8, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX9", SND_SOC_NOPM, WCD934X_TX9, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX10", SND_SOC_NOPM, WCD934X_TX10, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX11", SND_SOC_NOPM, WCD934X_TX11, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
	SOC_SINGLE_EXT("SLIM TX13", SND_SOC_NOPM, WCD934X_TX13, 1, 0,
		       slim_tx_mixer_get, slim_tx_mixer_put),
};

static const struct snd_kcontrol_new wcd934x_snd_controls[] = {
	/* Gain Controls */
	SOC_SINGLE_TLV("EAR PA Volume", WCD934X_ANA_EAR, 4, 4, 1, ear_pa_gain),
	SOC_SINGLE_TLV("HPHL Volume", WCD934X_HPH_L_EN, 0, 24, 1, line_gain),
	SOC_SINGLE_TLV("HPHR Volume", WCD934X_HPH_R_EN, 0, 24, 1, line_gain),
	SOC_SINGLE_TLV("LINEOUT1 Volume", WCD934X_DIFF_LO_LO1_COMPANDER,
		       3, 16, 1, line_gain),
	SOC_SINGLE_TLV("LINEOUT2 Volume", WCD934X_DIFF_LO_LO2_COMPANDER,
		       3, 16, 1, line_gain),

	SOC_SINGLE_TLV("ADC1 Volume", WCD934X_ANA_AMIC1, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC2 Volume", WCD934X_ANA_AMIC2, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC3 Volume", WCD934X_ANA_AMIC3, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC4 Volume", WCD934X_ANA_AMIC4, 0, 20, 0, analog_gain),

	SOC_SINGLE_S8_TLV("RX0 Digital Volume", WCD934X_CDC_RX0_RX_VOL_CTL,
			  -84, 40, digital_gain), /* -84dB min - 40dB max */
	SOC_SINGLE_S8_TLV("RX1 Digital Volume", WCD934X_CDC_RX1_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX2 Digital Volume", WCD934X_CDC_RX2_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX3 Digital Volume", WCD934X_CDC_RX3_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX4 Digital Volume", WCD934X_CDC_RX4_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX7 Digital Volume", WCD934X_CDC_RX7_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX8 Digital Volume", WCD934X_CDC_RX8_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX0 Mix Digital Volume",
			  WCD934X_CDC_RX0_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX1 Mix Digital Volume",
			  WCD934X_CDC_RX1_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX2 Mix Digital Volume",
			  WCD934X_CDC_RX2_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX3 Mix Digital Volume",
			  WCD934X_CDC_RX3_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX4 Mix Digital Volume",
			  WCD934X_CDC_RX4_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX7 Mix Digital Volume",
			  WCD934X_CDC_RX7_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX8 Mix Digital Volume",
			  WCD934X_CDC_RX8_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),

	SOC_SINGLE_S8_TLV("DEC0 Volume", WCD934X_CDC_TX0_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC1 Volume", WCD934X_CDC_TX1_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC2 Volume", WCD934X_CDC_TX2_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC3 Volume", WCD934X_CDC_TX3_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC4 Volume", WCD934X_CDC_TX4_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC5 Volume", WCD934X_CDC_TX5_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC6 Volume", WCD934X_CDC_TX6_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC7 Volume", WCD934X_CDC_TX7_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("DEC8 Volume", WCD934X_CDC_TX8_TX_VOL_CTL,
			  -84, 40, digital_gain),

	SOC_SINGLE_S8_TLV("IIR0 INP0 Volume",
			  WCD934X_CDC_SIDETONE_IIR0_IIR_GAIN_B1_CTL, -84, 40,
			  digital_gain),
	SOC_SINGLE_S8_TLV("IIR0 INP1 Volume",
			  WCD934X_CDC_SIDETONE_IIR0_IIR_GAIN_B2_CTL, -84, 40,
			  digital_gain),
	SOC_SINGLE_S8_TLV("IIR0 INP2 Volume",
			  WCD934X_CDC_SIDETONE_IIR0_IIR_GAIN_B3_CTL, -84, 40,
			  digital_gain),
	SOC_SINGLE_S8_TLV("IIR0 INP3 Volume",
			  WCD934X_CDC_SIDETONE_IIR0_IIR_GAIN_B4_CTL, -84, 40,
			  digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP0 Volume",
			  WCD934X_CDC_SIDETONE_IIR1_IIR_GAIN_B1_CTL, -84, 40,
			  digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP1 Volume",
			  WCD934X_CDC_SIDETONE_IIR1_IIR_GAIN_B2_CTL, -84, 40,
			  digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP2 Volume",
			  WCD934X_CDC_SIDETONE_IIR1_IIR_GAIN_B3_CTL, -84, 40,
			  digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP3 Volume",
			  WCD934X_CDC_SIDETONE_IIR1_IIR_GAIN_B4_CTL, -84, 40,
			  digital_gain),

	SOC_ENUM("TX0 HPF cut off", cf_dec0_enum),
	SOC_ENUM("TX1 HPF cut off", cf_dec1_enum),
	SOC_ENUM("TX2 HPF cut off", cf_dec2_enum),
	SOC_ENUM("TX3 HPF cut off", cf_dec3_enum),
	SOC_ENUM("TX4 HPF cut off", cf_dec4_enum),
	SOC_ENUM("TX5 HPF cut off", cf_dec5_enum),
	SOC_ENUM("TX6 HPF cut off", cf_dec6_enum),
	SOC_ENUM("TX7 HPF cut off", cf_dec7_enum),
	SOC_ENUM("TX8 HPF cut off", cf_dec8_enum),

	SOC_ENUM("RX INT0_1 HPF cut off", cf_int0_1_enum),
	SOC_ENUM("RX INT0_2 HPF cut off", cf_int0_2_enum),
	SOC_ENUM("RX INT1_1 HPF cut off", cf_int1_1_enum),
	SOC_ENUM("RX INT1_2 HPF cut off", cf_int1_2_enum),
	SOC_ENUM("RX INT2_1 HPF cut off", cf_int2_1_enum),
	SOC_ENUM("RX INT2_2 HPF cut off", cf_int2_2_enum),
	SOC_ENUM("RX INT3_1 HPF cut off", cf_int3_1_enum),
	SOC_ENUM("RX INT3_2 HPF cut off", cf_int3_2_enum),
	SOC_ENUM("RX INT4_1 HPF cut off", cf_int4_1_enum),
	SOC_ENUM("RX INT4_2 HPF cut off", cf_int4_2_enum),
	SOC_ENUM("RX INT7_1 HPF cut off", cf_int7_1_enum),
	SOC_ENUM("RX INT7_2 HPF cut off", cf_int7_2_enum),
	SOC_ENUM("RX INT8_1 HPF cut off", cf_int8_1_enum),
	SOC_ENUM("RX INT8_2 HPF cut off", cf_int8_2_enum),

	SOC_ENUM_EXT("RX HPH Mode", rx_hph_mode_mux_enum,
		     wcd934x_rx_hph_mode_get, wcd934x_rx_hph_mode_put),

	SOC_SINGLE("IIR1 Band1 Switch", WCD934X_CDC_SIDETONE_IIR0_IIR_CTL,
		   0, 1, 0),
	SOC_SINGLE("IIR1 Band2 Switch", WCD934X_CDC_SIDETONE_IIR0_IIR_CTL,
		   1, 1, 0),
	SOC_SINGLE("IIR1 Band3 Switch", WCD934X_CDC_SIDETONE_IIR0_IIR_CTL,
		   2, 1, 0),
	SOC_SINGLE("IIR1 Band4 Switch", WCD934X_CDC_SIDETONE_IIR0_IIR_CTL,
		   3, 1, 0),
	SOC_SINGLE("IIR1 Band5 Switch", WCD934X_CDC_SIDETONE_IIR0_IIR_CTL,
		   4, 1, 0),
	SOC_SINGLE("IIR2 Band1 Switch", WCD934X_CDC_SIDETONE_IIR1_IIR_CTL,
		   0, 1, 0),
	SOC_SINGLE("IIR2 Band2 Switch", WCD934X_CDC_SIDETONE_IIR1_IIR_CTL,
		   1, 1, 0),
	SOC_SINGLE("IIR2 Band3 Switch", WCD934X_CDC_SIDETONE_IIR1_IIR_CTL,
		   2, 1, 0),
	SOC_SINGLE("IIR2 Band4 Switch", WCD934X_CDC_SIDETONE_IIR1_IIR_CTL,
		   3, 1, 0),
	SOC_SINGLE("IIR2 Band5 Switch", WCD934X_CDC_SIDETONE_IIR1_IIR_CTL,
		   4, 1, 0),
	WCD_IIR_FILTER_CTL("IIR0 Band1", IIR0, BAND1),
	WCD_IIR_FILTER_CTL("IIR0 Band2", IIR0, BAND2),
	WCD_IIR_FILTER_CTL("IIR0 Band3", IIR0, BAND3),
	WCD_IIR_FILTER_CTL("IIR0 Band4", IIR0, BAND4),
	WCD_IIR_FILTER_CTL("IIR0 Band5", IIR0, BAND5),

	WCD_IIR_FILTER_CTL("IIR1 Band1", IIR1, BAND1),
	WCD_IIR_FILTER_CTL("IIR1 Band2", IIR1, BAND2),
	WCD_IIR_FILTER_CTL("IIR1 Band3", IIR1, BAND3),
	WCD_IIR_FILTER_CTL("IIR1 Band4", IIR1, BAND4),
	WCD_IIR_FILTER_CTL("IIR1 Band5", IIR1, BAND5),

	SOC_SINGLE_EXT("COMP1 Switch", SND_SOC_NOPM, COMPANDER_1, 1, 0,
		       wcd934x_compander_get, wcd934x_compander_set),
	SOC_SINGLE_EXT("COMP2 Switch", SND_SOC_NOPM, COMPANDER_2, 1, 0,
		       wcd934x_compander_get, wcd934x_compander_set),
	SOC_SINGLE_EXT("COMP3 Switch", SND_SOC_NOPM, COMPANDER_3, 1, 0,
		       wcd934x_compander_get, wcd934x_compander_set),
	SOC_SINGLE_EXT("COMP4 Switch", SND_SOC_NOPM, COMPANDER_4, 1, 0,
		       wcd934x_compander_get, wcd934x_compander_set),
	SOC_SINGLE_EXT("COMP7 Switch", SND_SOC_NOPM, COMPANDER_7, 1, 0,
		       wcd934x_compander_get, wcd934x_compander_set),
	SOC_SINGLE_EXT("COMP8 Switch", SND_SOC_NOPM, COMPANDER_8, 1, 0,
		       wcd934x_compander_get, wcd934x_compander_set),
};

static void wcd934x_codec_enable_int_port(struct wcd_slim_codec_dai_data *dai,
					  struct snd_soc_component *component)
{
	int port_num = 0;
	unsigned short reg = 0;
	unsigned int val = 0;
	struct wcd934x_codec *wcd = dev_get_drvdata(component->dev);
	struct wcd934x_slim_ch *ch;

	list_for_each_entry(ch, &dai->slim_ch_list, list) {
		if (ch->port >= WCD934X_RX_START) {
			port_num = ch->port - WCD934X_RX_START;
			reg = WCD934X_SLIM_PGD_PORT_INT_EN0 + (port_num / 8);
		} else {
			port_num = ch->port;
			reg = WCD934X_SLIM_PGD_PORT_INT_TX_EN0 + (port_num / 8);
		}

		regmap_read(wcd->if_regmap, reg, &val);
		if (!(val & BIT(port_num % 8)))
			regmap_write(wcd->if_regmap, reg,
				     val | BIT(port_num % 8));
	}
}

static int wcd934x_codec_enable_slim(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(comp);
	struct wcd_slim_codec_dai_data *dai = &wcd->dai[w->shift];

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		wcd934x_codec_enable_int_port(dai, comp);
		break;
	}

	return 0;
}

static void wcd934x_codec_hd2_control(struct snd_soc_component *component,
				      u16 interp_idx, int event)
{
	u16 hd2_scale_reg;
	u16 hd2_enable_reg = 0;

	switch (interp_idx) {
	case INTERP_HPHL:
		hd2_scale_reg = WCD934X_CDC_RX1_RX_PATH_SEC3;
		hd2_enable_reg = WCD934X_CDC_RX1_RX_PATH_CFG0;
		break;
	case INTERP_HPHR:
		hd2_scale_reg = WCD934X_CDC_RX2_RX_PATH_SEC3;
		hd2_enable_reg = WCD934X_CDC_RX2_RX_PATH_CFG0;
		break;
	default:
		return;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(component, hd2_scale_reg,
				      WCD934X_CDC_RX_PATH_SEC_HD2_ALPHA_MASK,
				      WCD934X_CDC_RX_PATH_SEC_HD2_ALPHA_0P3125);
		snd_soc_component_update_bits(component, hd2_enable_reg,
				      WCD934X_CDC_RX_PATH_CFG_HD2_EN_MASK,
				      WCD934X_CDC_RX_PATH_CFG_HD2_ENABLE);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(component, hd2_enable_reg,
				      WCD934X_CDC_RX_PATH_CFG_HD2_EN_MASK,
				      WCD934X_CDC_RX_PATH_CFG_HD2_DISABLE);
		snd_soc_component_update_bits(component, hd2_scale_reg,
				      WCD934X_CDC_RX_PATH_SEC_HD2_ALPHA_MASK,
				      WCD934X_CDC_RX_PATH_SEC_HD2_ALPHA_0P0000);
	}
}

static void wcd934x_codec_hphdelay_lutbypass(struct snd_soc_component *comp,
					     u16 interp_idx, int event)
{
	u8 hph_dly_mask;
	u16 hph_lut_bypass_reg = 0;

	switch (interp_idx) {
	case INTERP_HPHL:
		hph_dly_mask = 1;
		hph_lut_bypass_reg = WCD934X_CDC_TOP_HPHL_COMP_LUT;
		break;
	case INTERP_HPHR:
		hph_dly_mask = 2;
		hph_lut_bypass_reg = WCD934X_CDC_TOP_HPHR_COMP_LUT;
		break;
	default:
		return;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(comp, WCD934X_CDC_CLSH_TEST0,
					      hph_dly_mask, 0x0);
		snd_soc_component_update_bits(comp, hph_lut_bypass_reg,
					      WCD934X_HPH_LUT_BYPASS_MASK,
					      WCD934X_HPH_LUT_BYPASS_ENABLE);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(comp, WCD934X_CDC_CLSH_TEST0,
					      hph_dly_mask, hph_dly_mask);
		snd_soc_component_update_bits(comp, hph_lut_bypass_reg,
					      WCD934X_HPH_LUT_BYPASS_MASK,
					      WCD934X_HPH_LUT_BYPASS_DISABLE);
	}
}

static int wcd934x_config_compander(struct snd_soc_component *comp,
				    int interp_n, int event)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	int compander;
	u16 comp_ctl0_reg, rx_path_cfg0_reg;

	/* EAR does not have compander */
	if (!interp_n)
		return 0;

	compander = interp_n - 1;
	if (!wcd->comp_enabled[compander])
		return 0;

	comp_ctl0_reg = WCD934X_CDC_COMPANDER1_CTL0 + (compander * 8);
	rx_path_cfg0_reg = WCD934X_CDC_RX1_RX_PATH_CFG0 + (compander * 20);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable Compander Clock */
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_CLK_EN_MASK,
					      WCD934X_COMP_CLK_ENABLE);
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_SOFT_RST_MASK,
					      WCD934X_COMP_SOFT_RST_ENABLE);
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_SOFT_RST_MASK,
					      WCD934X_COMP_SOFT_RST_DISABLE);
		snd_soc_component_update_bits(comp, rx_path_cfg0_reg,
					      WCD934X_HPH_CMP_EN_MASK,
					      WCD934X_HPH_CMP_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(comp, rx_path_cfg0_reg,
					      WCD934X_HPH_CMP_EN_MASK,
					      WCD934X_HPH_CMP_DISABLE);
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_HALT_MASK,
					      WCD934X_COMP_HALT);
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_SOFT_RST_MASK,
					      WCD934X_COMP_SOFT_RST_ENABLE);
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_SOFT_RST_MASK,
					      WCD934X_COMP_SOFT_RST_DISABLE);
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_CLK_EN_MASK, 0x0);
		snd_soc_component_update_bits(comp, comp_ctl0_reg,
					      WCD934X_COMP_SOFT_RST_MASK, 0x0);
		break;
	}

	return 0;
}

static int wcd934x_codec_enable_interp_clk(struct snd_soc_dapm_widget *w,
					 struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	int interp_idx = w->shift;
	u16 main_reg = WCD934X_CDC_RX0_RX_PATH_CTL + (interp_idx * 20);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Clk enable */
		snd_soc_component_update_bits(comp, main_reg,
					     WCD934X_RX_CLK_EN_MASK,
					     WCD934X_RX_CLK_ENABLE);
		wcd934x_codec_hd2_control(comp, interp_idx, event);
		wcd934x_codec_hphdelay_lutbypass(comp, interp_idx, event);
		wcd934x_config_compander(comp, interp_idx, event);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd934x_config_compander(comp, interp_idx, event);
		wcd934x_codec_hphdelay_lutbypass(comp, interp_idx, event);
		wcd934x_codec_hd2_control(comp, interp_idx, event);
		/* Clk Disable */
		snd_soc_component_update_bits(comp, main_reg,
					     WCD934X_RX_CLK_EN_MASK, 0);
		/* Reset enable and disable */
		snd_soc_component_update_bits(comp, main_reg,
					      WCD934X_RX_RESET_MASK,
					      WCD934X_RX_RESET_ENABLE);
		snd_soc_component_update_bits(comp, main_reg,
					      WCD934X_RX_RESET_MASK,
					      WCD934X_RX_RESET_DISABLE);
		/* Reset rate to 48K*/
		snd_soc_component_update_bits(comp, main_reg,
					      WCD934X_RX_PCM_RATE_MASK,
					      WCD934X_RX_PCM_RATE_F_48K);
		break;
	}

	return 0;
}

static int wcd934x_codec_enable_mix_path(struct snd_soc_dapm_widget *w,
					 struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	int offset_val = 0;
	u16 gain_reg, mix_reg;
	int val = 0;

	gain_reg = WCD934X_CDC_RX0_RX_VOL_MIX_CTL +
					(w->shift * WCD934X_RX_PATH_CTL_OFFSET);
	mix_reg = WCD934X_CDC_RX0_RX_PATH_MIX_CTL +
					(w->shift * WCD934X_RX_PATH_CTL_OFFSET);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Clk enable */
		snd_soc_component_update_bits(comp, mix_reg,
					      WCD934X_CDC_RX_MIX_CLK_EN_MASK,
					      WCD934X_CDC_RX_MIX_CLK_ENABLE);
		break;

	case SND_SOC_DAPM_POST_PMU:
		val = snd_soc_component_read(comp, gain_reg);
		val += offset_val;
		snd_soc_component_write(comp, gain_reg, val);
		break;
	}

	return 0;
}

static int wcd934x_codec_set_iir_gain(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	int reg = w->reg;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* B1 GAIN */
		snd_soc_component_write(comp, reg,
					snd_soc_component_read(comp, reg));
		/* B2 GAIN */
		reg++;
		snd_soc_component_write(comp, reg,
					snd_soc_component_read(comp, reg));
		/* B3 GAIN */
		reg++;
		snd_soc_component_write(comp, reg,
					snd_soc_component_read(comp, reg));
		/* B4 GAIN */
		reg++;
		snd_soc_component_write(comp, reg,
					snd_soc_component_read(comp, reg));
		/* B5 GAIN */
		reg++;
		snd_soc_component_write(comp, reg,
					snd_soc_component_read(comp, reg));
		break;
	default:
		break;
	}
	return 0;
}

static int wcd934x_codec_enable_main_path(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *kcontrol,
					  int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	u16 gain_reg;

	gain_reg = WCD934X_CDC_RX0_RX_VOL_CTL + (w->shift *
						 WCD934X_RX_PATH_CTL_OFFSET);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write(comp, gain_reg,
				snd_soc_component_read(comp, gain_reg));
		break;
	}

	return 0;
}

static int wcd934x_codec_ear_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Disable AutoChop timer during power up */
		snd_soc_component_update_bits(comp,
				      WCD934X_HPH_NEW_INT_HPH_TIMER1,
				      WCD934X_HPH_AUTOCHOP_TIMER_EN_MASK, 0x0);
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_EAR, CLS_H_NORMAL);

		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_EAR, CLS_H_NORMAL);
		break;
	}

	return 0;
}

static int wcd934x_codec_hphl_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	int hph_mode = wcd->hph_mode;
	u8 dem_inp;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Read DEM INP Select */
		dem_inp = snd_soc_component_read(comp,
				   WCD934X_CDC_RX1_RX_PATH_SEC0) & 0x03;

		if (((hph_mode == CLS_H_HIFI) || (hph_mode == CLS_H_LOHIFI) ||
		     (hph_mode == CLS_H_LP)) && (dem_inp != 0x01)) {
			return -EINVAL;
		}
		if (hph_mode != CLS_H_LP)
			/* Ripple freq control enable */
			snd_soc_component_update_bits(comp,
					WCD934X_SIDO_NEW_VOUT_D_FREQ2,
					WCD934X_SIDO_RIPPLE_FREQ_EN_MASK,
					WCD934X_SIDO_RIPPLE_FREQ_ENABLE);
		/* Disable AutoChop timer during power up */
		snd_soc_component_update_bits(comp,
				      WCD934X_HPH_NEW_INT_HPH_TIMER1,
				      WCD934X_HPH_AUTOCHOP_TIMER_EN_MASK, 0x0);
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHL, hph_mode);

		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 1000us required as per HW requirement */
		usleep_range(1000, 1100);
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHL, hph_mode);
		if (hph_mode != CLS_H_LP)
			/* Ripple freq control disable */
			snd_soc_component_update_bits(comp,
					WCD934X_SIDO_NEW_VOUT_D_FREQ2,
					WCD934X_SIDO_RIPPLE_FREQ_EN_MASK, 0x0);

		break;
	default:
		break;
	}

	return 0;
}

static int wcd934x_codec_hphr_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	int hph_mode = wcd->hph_mode;
	u8 dem_inp;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dem_inp = snd_soc_component_read(comp,
					WCD934X_CDC_RX2_RX_PATH_SEC0) & 0x03;
		if (((hph_mode == CLS_H_HIFI) || (hph_mode == CLS_H_LOHIFI) ||
		     (hph_mode == CLS_H_LP)) && (dem_inp != 0x01)) {
			return -EINVAL;
		}
		if (hph_mode != CLS_H_LP)
			/* Ripple freq control enable */
			snd_soc_component_update_bits(comp,
					WCD934X_SIDO_NEW_VOUT_D_FREQ2,
					WCD934X_SIDO_RIPPLE_FREQ_EN_MASK,
					WCD934X_SIDO_RIPPLE_FREQ_ENABLE);
		/* Disable AutoChop timer during power up */
		snd_soc_component_update_bits(comp,
				      WCD934X_HPH_NEW_INT_HPH_TIMER1,
				      WCD934X_HPH_AUTOCHOP_TIMER_EN_MASK, 0x0);
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHR,
			     hph_mode);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 1000us required as per HW requirement */
		usleep_range(1000, 1100);

		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHR, hph_mode);
		if (hph_mode != CLS_H_LP)
			/* Ripple freq control disable */
			snd_soc_component_update_bits(comp,
					WCD934X_SIDO_NEW_VOUT_D_FREQ2,
					WCD934X_SIDO_RIPPLE_FREQ_EN_MASK, 0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int wcd934x_codec_lineout_dac_event(struct snd_soc_dapm_widget *w,
					   struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_LO, CLS_AB);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_LO, CLS_AB);
		break;
	}

	return 0;
}

static int wcd934x_codec_enable_hphl_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(comp);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required after PA is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is needed.
		 */
		usleep_range(20000, 20100);

		snd_soc_component_update_bits(comp, WCD934X_HPH_L_TEST,
					      WCD934X_HPH_OCP_DET_MASK,
					      WCD934X_HPH_OCP_DET_ENABLE);
		/* Remove Mute on primary path */
		snd_soc_component_update_bits(comp, WCD934X_CDC_RX1_RX_PATH_CTL,
				      WCD934X_RX_PATH_PGA_MUTE_EN_MASK,
				      0);
		/* Enable GM3 boost */
		snd_soc_component_update_bits(comp, WCD934X_HPH_CNP_WG_CTL,
					      WCD934X_HPH_GM3_BOOST_EN_MASK,
					      WCD934X_HPH_GM3_BOOST_ENABLE);
		/* Enable AutoChop timer at the end of power up */
		snd_soc_component_update_bits(comp,
				      WCD934X_HPH_NEW_INT_HPH_TIMER1,
				      WCD934X_HPH_AUTOCHOP_TIMER_EN_MASK,
				      WCD934X_HPH_AUTOCHOP_TIMER_ENABLE);
		/* Remove mix path mute */
		snd_soc_component_update_bits(comp,
				WCD934X_CDC_RX1_RX_PATH_MIX_CTL,
				WCD934X_CDC_RX_PGA_MUTE_EN_MASK, 0x00);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		wcd_mbhc_event_notify(wcd->mbhc, WCD_EVENT_POST_HPHL_PA_OFF);
		/* Enable DSD Mute before PA disable */
		snd_soc_component_update_bits(comp, WCD934X_HPH_L_TEST,
					      WCD934X_HPH_OCP_DET_MASK,
					      WCD934X_HPH_OCP_DET_DISABLE);
		snd_soc_component_update_bits(comp, WCD934X_CDC_RX1_RX_PATH_CTL,
					      WCD934X_RX_PATH_PGA_MUTE_EN_MASK,
					      WCD934X_RX_PATH_PGA_MUTE_ENABLE);
		snd_soc_component_update_bits(comp,
					      WCD934X_CDC_RX1_RX_PATH_MIX_CTL,
					      WCD934X_RX_PATH_PGA_MUTE_EN_MASK,
					      WCD934X_RX_PATH_PGA_MUTE_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/*
		 * 5ms sleep is required after PA disable. If compander is
		 * disabled, then 20ms delay is needed after PA disable.
		 */
		usleep_range(20000, 20100);
		wcd_mbhc_event_notify(wcd->mbhc, WCD_EVENT_POST_HPHL_PA_OFF);
		break;
	}

	return 0;
}

static int wcd934x_codec_enable_hphr_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = snd_soc_component_get_drvdata(comp);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required after PA is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is needed.
		 */
		usleep_range(20000, 20100);
		snd_soc_component_update_bits(comp, WCD934X_HPH_R_TEST,
					      WCD934X_HPH_OCP_DET_MASK,
					      WCD934X_HPH_OCP_DET_ENABLE);
		/* Remove mute */
		snd_soc_component_update_bits(comp, WCD934X_CDC_RX2_RX_PATH_CTL,
					      WCD934X_RX_PATH_PGA_MUTE_EN_MASK,
					      0);
		/* Enable GM3 boost */
		snd_soc_component_update_bits(comp, WCD934X_HPH_CNP_WG_CTL,
					      WCD934X_HPH_GM3_BOOST_EN_MASK,
					      WCD934X_HPH_GM3_BOOST_ENABLE);
		/* Enable AutoChop timer at the end of power up */
		snd_soc_component_update_bits(comp,
				      WCD934X_HPH_NEW_INT_HPH_TIMER1,
				      WCD934X_HPH_AUTOCHOP_TIMER_EN_MASK,
				      WCD934X_HPH_AUTOCHOP_TIMER_ENABLE);
		/* Remove mix path mute if it is enabled */
		if ((snd_soc_component_read(comp,
				      WCD934X_CDC_RX2_RX_PATH_MIX_CTL)) & 0x10)
			snd_soc_component_update_bits(comp,
					      WCD934X_CDC_RX2_RX_PATH_MIX_CTL,
					      WCD934X_CDC_RX_PGA_MUTE_EN_MASK,
					      WCD934X_CDC_RX_PGA_MUTE_DISABLE);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		wcd_mbhc_event_notify(wcd->mbhc, WCD_EVENT_PRE_HPHR_PA_OFF);
		snd_soc_component_update_bits(comp, WCD934X_HPH_R_TEST,
					      WCD934X_HPH_OCP_DET_MASK,
					      WCD934X_HPH_OCP_DET_DISABLE);
		snd_soc_component_update_bits(comp, WCD934X_CDC_RX2_RX_PATH_CTL,
					      WCD934X_RX_PATH_PGA_MUTE_EN_MASK,
					      WCD934X_RX_PATH_PGA_MUTE_ENABLE);
		snd_soc_component_update_bits(comp,
					      WCD934X_CDC_RX2_RX_PATH_MIX_CTL,
					      WCD934X_CDC_RX_PGA_MUTE_EN_MASK,
					      WCD934X_CDC_RX_PGA_MUTE_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/*
		 * 5ms sleep is required after PA disable. If compander is
		 * disabled, then 20ms delay is needed after PA disable.
		 */
		usleep_range(20000, 20100);
		wcd_mbhc_event_notify(wcd->mbhc, WCD_EVENT_POST_HPHR_PA_OFF);
		break;
	}

	return 0;
}

static u32 wcd934x_get_dmic_sample_rate(struct snd_soc_component *comp,
					unsigned int dmic,
				      struct wcd934x_codec *wcd)
{
	u8 tx_stream_fs;
	u8 adc_mux_index = 0, adc_mux_sel = 0;
	bool dec_found = false;
	u16 adc_mux_ctl_reg, tx_fs_reg;
	u32 dmic_fs;

	while (!dec_found && adc_mux_index < WCD934X_MAX_VALID_ADC_MUX) {
		if (adc_mux_index < 4) {
			adc_mux_ctl_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG0 +
						(adc_mux_index * 2);
		} else if (adc_mux_index < WCD934X_INVALID_ADC_MUX) {
			adc_mux_ctl_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0 +
						adc_mux_index - 4;
		} else if (adc_mux_index == WCD934X_INVALID_ADC_MUX) {
			++adc_mux_index;
			continue;
		}
		adc_mux_sel = ((snd_soc_component_read(comp, adc_mux_ctl_reg)
			       & 0xF8) >> 3) - 1;

		if (adc_mux_sel == dmic) {
			dec_found = true;
			break;
		}

		++adc_mux_index;
	}

	if (dec_found && adc_mux_index <= 8) {
		tx_fs_reg = WCD934X_CDC_TX0_TX_PATH_CTL + (16 * adc_mux_index);
		tx_stream_fs = snd_soc_component_read(comp, tx_fs_reg) & 0x0F;
		if (tx_stream_fs <= 4)
			dmic_fs = min(wcd->dmic_sample_rate, WCD9XXX_DMIC_SAMPLE_RATE_2P4MHZ);
		else
			dmic_fs = WCD9XXX_DMIC_SAMPLE_RATE_4P8MHZ;
	} else {
		dmic_fs = wcd->dmic_sample_rate;
	}

	return dmic_fs;
}

static u8 wcd934x_get_dmic_clk_val(struct snd_soc_component *comp,
				   u32 mclk_rate, u32 dmic_clk_rate)
{
	u32 div_factor;
	u8 dmic_ctl_val;

	/* Default value to return in case of error */
	if (mclk_rate == WCD934X_MCLK_CLK_9P6MHZ)
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_2;
	else
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_3;

	if (dmic_clk_rate == 0) {
		dev_err(comp->dev,
			"%s: dmic_sample_rate cannot be 0\n",
			__func__);
		goto done;
	}

	div_factor = mclk_rate / dmic_clk_rate;
	switch (div_factor) {
	case 2:
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_2;
		break;
	case 3:
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_3;
		break;
	case 4:
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_4;
		break;
	case 6:
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_6;
		break;
	case 8:
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_8;
		break;
	case 16:
		dmic_ctl_val = WCD934X_DMIC_CLK_DIV_16;
		break;
	default:
		dev_err(comp->dev,
			"%s: Invalid div_factor %u, clk_rate(%u), dmic_rate(%u)\n",
			__func__, div_factor, mclk_rate, dmic_clk_rate);
		break;
	}

done:
	return dmic_ctl_val;
}

static int wcd934x_codec_enable_dmic(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	u8  dmic_clk_en = 0x01;
	u16 dmic_clk_reg;
	s32 *dmic_clk_cnt;
	u8 dmic_rate_val, dmic_rate_shift = 1;
	unsigned int dmic;
	u32 dmic_sample_rate;
	int ret;
	char *wname;

	wname = strpbrk(w->name, "012345");
	if (!wname) {
		dev_err(comp->dev, "%s: widget not found\n", __func__);
		return -EINVAL;
	}

	ret = kstrtouint(wname, 10, &dmic);
	if (ret < 0) {
		dev_err(comp->dev, "%s: Invalid DMIC line on the codec\n",
			__func__);
		return -EINVAL;
	}

	switch (dmic) {
	case 0:
	case 1:
		dmic_clk_cnt = &wcd->dmic_0_1_clk_cnt;
		dmic_clk_reg = WCD934X_CPE_SS_DMIC0_CTL;
		break;
	case 2:
	case 3:
		dmic_clk_cnt = &wcd->dmic_2_3_clk_cnt;
		dmic_clk_reg = WCD934X_CPE_SS_DMIC1_CTL;
		break;
	case 4:
	case 5:
		dmic_clk_cnt = &wcd->dmic_4_5_clk_cnt;
		dmic_clk_reg = WCD934X_CPE_SS_DMIC2_CTL;
		break;
	default:
		dev_err(comp->dev, "%s: Invalid DMIC Selection\n",
			__func__);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dmic_sample_rate = wcd934x_get_dmic_sample_rate(comp, dmic,
								wcd);
		dmic_rate_val = wcd934x_get_dmic_clk_val(comp, wcd->rate,
							 dmic_sample_rate);
		(*dmic_clk_cnt)++;
		if (*dmic_clk_cnt == 1) {
			dmic_rate_val = dmic_rate_val << dmic_rate_shift;
			snd_soc_component_update_bits(comp, dmic_clk_reg,
						      WCD934X_DMIC_RATE_MASK,
						      dmic_rate_val);
			snd_soc_component_update_bits(comp, dmic_clk_reg,
						      dmic_clk_en, dmic_clk_en);
		}

		break;
	case SND_SOC_DAPM_POST_PMD:
		(*dmic_clk_cnt)--;
		if (*dmic_clk_cnt == 0)
			snd_soc_component_update_bits(comp, dmic_clk_reg,
						      dmic_clk_en, 0);
		break;
	}

	return 0;
}

static int wcd934x_codec_find_amic_input(struct snd_soc_component *comp,
					 int adc_mux_n)
{
	u16 mask, shift, adc_mux_in_reg;
	u16 amic_mux_sel_reg;
	bool is_amic;

	if (adc_mux_n < 0 || adc_mux_n > WCD934X_MAX_VALID_ADC_MUX ||
	    adc_mux_n == WCD934X_INVALID_ADC_MUX)
		return 0;

	if (adc_mux_n < 3) {
		adc_mux_in_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
				 adc_mux_n;
		mask = 0x03;
		shift = 0;
		amic_mux_sel_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG0 +
				   2 * adc_mux_n;
	} else if (adc_mux_n < 4) {
		adc_mux_in_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG1;
		mask = 0x03;
		shift = 0;
		amic_mux_sel_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG0 +
				   2 * adc_mux_n;
	} else if (adc_mux_n < 7) {
		adc_mux_in_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
				 (adc_mux_n - 4);
		mask = 0x0C;
		shift = 2;
		amic_mux_sel_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0 +
				   adc_mux_n - 4;
	} else if (adc_mux_n < 8) {
		adc_mux_in_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG1;
		mask = 0x0C;
		shift = 2;
		amic_mux_sel_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0 +
				   adc_mux_n - 4;
	} else if (adc_mux_n < 12) {
		adc_mux_in_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
				 ((adc_mux_n == 8) ? (adc_mux_n - 8) :
				  (adc_mux_n - 9));
		mask = 0x30;
		shift = 4;
		amic_mux_sel_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0 +
				   adc_mux_n - 4;
	} else if (adc_mux_n < 13) {
		adc_mux_in_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX3_CFG1;
		mask = 0x30;
		shift = 4;
		amic_mux_sel_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0 +
				   adc_mux_n - 4;
	} else {
		adc_mux_in_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX0_CFG1;
		mask = 0xC0;
		shift = 6;
		amic_mux_sel_reg = WCD934X_CDC_TX_INP_MUX_ADC_MUX4_CFG0 +
				   adc_mux_n - 4;
	}

	is_amic = (((snd_soc_component_read(comp, adc_mux_in_reg)
		     & mask) >> shift) == 1);
	if (!is_amic)
		return 0;

	return snd_soc_component_read(comp, amic_mux_sel_reg) & 0x07;
}

static u16 wcd934x_codec_get_amic_pwlvl_reg(struct snd_soc_component *comp,
					    int amic)
{
	u16 pwr_level_reg = 0;

	switch (amic) {
	case 1:
	case 2:
		pwr_level_reg = WCD934X_ANA_AMIC1;
		break;

	case 3:
	case 4:
		pwr_level_reg = WCD934X_ANA_AMIC3;
		break;
	default:
		break;
	}

	return pwr_level_reg;
}

static int wcd934x_codec_enable_dec(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	unsigned int decimator;
	char *dec_adc_mux_name = NULL;
	char *widget_name;
	int ret = 0, amic_n;
	u16 tx_vol_ctl_reg, pwr_level_reg = 0, dec_cfg_reg, hpf_gate_reg;
	u16 tx_gain_ctl_reg;
	char *dec;
	u8 hpf_coff_freq;

	char *wname __free(kfree) = kstrndup(w->name, 15, GFP_KERNEL);
	if (!wname)
		return -ENOMEM;

	widget_name = wname;
	dec_adc_mux_name = strsep(&widget_name, " ");
	if (!dec_adc_mux_name) {
		dev_err(comp->dev, "%s: Invalid decimator = %s\n",
			__func__, w->name);
		return -EINVAL;
	}
	dec_adc_mux_name = widget_name;

	dec = strpbrk(dec_adc_mux_name, "012345678");
	if (!dec) {
		dev_err(comp->dev, "%s: decimator index not found\n",
			__func__);
		return -EINVAL;
	}

	ret = kstrtouint(dec, 10, &decimator);
	if (ret < 0) {
		dev_err(comp->dev, "%s: Invalid decimator = %s\n",
			__func__, wname);
		return -EINVAL;
	}

	tx_vol_ctl_reg = WCD934X_CDC_TX0_TX_PATH_CTL + 16 * decimator;
	hpf_gate_reg = WCD934X_CDC_TX0_TX_PATH_SEC2 + 16 * decimator;
	dec_cfg_reg = WCD934X_CDC_TX0_TX_PATH_CFG0 + 16 * decimator;
	tx_gain_ctl_reg = WCD934X_CDC_TX0_TX_VOL_CTL + 16 * decimator;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		amic_n = wcd934x_codec_find_amic_input(comp, decimator);
		if (amic_n)
			pwr_level_reg = wcd934x_codec_get_amic_pwlvl_reg(comp,
								 amic_n);

		if (!pwr_level_reg)
			break;

		switch ((snd_soc_component_read(comp, pwr_level_reg) &
				      WCD934X_AMIC_PWR_LVL_MASK) >>
				      WCD934X_AMIC_PWR_LVL_SHIFT) {
		case WCD934X_AMIC_PWR_LEVEL_LP:
			snd_soc_component_update_bits(comp, dec_cfg_reg,
					WCD934X_DEC_PWR_LVL_MASK,
					WCD934X_DEC_PWR_LVL_LP);
			break;
		case WCD934X_AMIC_PWR_LEVEL_HP:
			snd_soc_component_update_bits(comp, dec_cfg_reg,
					WCD934X_DEC_PWR_LVL_MASK,
					WCD934X_DEC_PWR_LVL_HP);
			break;
		case WCD934X_AMIC_PWR_LEVEL_DEFAULT:
		case WCD934X_AMIC_PWR_LEVEL_HYBRID:
		default:
			snd_soc_component_update_bits(comp, dec_cfg_reg,
					WCD934X_DEC_PWR_LVL_MASK,
					WCD934X_DEC_PWR_LVL_DF);
			break;
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		hpf_coff_freq = (snd_soc_component_read(comp, dec_cfg_reg) &
				 TX_HPF_CUT_OFF_FREQ_MASK) >> 5;
		if (hpf_coff_freq != CF_MIN_3DB_150HZ) {
			snd_soc_component_update_bits(comp, dec_cfg_reg,
						      TX_HPF_CUT_OFF_FREQ_MASK,
						      CF_MIN_3DB_150HZ << 5);
			snd_soc_component_update_bits(comp, hpf_gate_reg,
				      WCD934X_HPH_CUTOFF_FREQ_CHANGE_REQ_MASK,
				      WCD934X_HPH_CUTOFF_FREQ_CHANGE_REQ);
			/*
			 * Minimum 1 clk cycle delay is required as per
			 * HW spec.
			 */
			usleep_range(1000, 1010);
			snd_soc_component_update_bits(comp, hpf_gate_reg,
				      WCD934X_HPH_CUTOFF_FREQ_CHANGE_REQ_MASK,
				      0);
		}
		/* apply gain after decimator is enabled */
		snd_soc_component_write(comp, tx_gain_ctl_reg,
					snd_soc_component_read(comp,
							 tx_gain_ctl_reg));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		hpf_coff_freq = (snd_soc_component_read(comp, dec_cfg_reg) &
				 TX_HPF_CUT_OFF_FREQ_MASK) >> 5;

		if (hpf_coff_freq != CF_MIN_3DB_150HZ) {
			snd_soc_component_update_bits(comp, dec_cfg_reg,
						      TX_HPF_CUT_OFF_FREQ_MASK,
						      hpf_coff_freq << 5);
			snd_soc_component_update_bits(comp, hpf_gate_reg,
				      WCD934X_HPH_CUTOFF_FREQ_CHANGE_REQ_MASK,
				      WCD934X_HPH_CUTOFF_FREQ_CHANGE_REQ);
				/*
				 * Minimum 1 clk cycle delay is required as per
				 * HW spec.
				 */
			usleep_range(1000, 1010);
			snd_soc_component_update_bits(comp, hpf_gate_reg,
				      WCD934X_HPH_CUTOFF_FREQ_CHANGE_REQ_MASK,
				      0);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(comp, tx_vol_ctl_reg,
					      0x10, 0x00);
		snd_soc_component_update_bits(comp, dec_cfg_reg,
					      WCD934X_DEC_PWR_LVL_MASK,
					      WCD934X_DEC_PWR_LVL_DF);
		break;
	}

	return ret;
}

static void wcd934x_codec_set_tx_hold(struct snd_soc_component *comp,
				      u16 amic_reg, bool set)
{
	u8 mask = 0x20;
	u8 val;

	if (amic_reg == WCD934X_ANA_AMIC1 ||
	    amic_reg == WCD934X_ANA_AMIC3)
		mask = 0x40;

	val = set ? mask : 0x00;

	switch (amic_reg) {
	case WCD934X_ANA_AMIC1:
	case WCD934X_ANA_AMIC2:
		snd_soc_component_update_bits(comp, WCD934X_ANA_AMIC2,
					      mask, val);
		break;
	case WCD934X_ANA_AMIC3:
	case WCD934X_ANA_AMIC4:
		snd_soc_component_update_bits(comp, WCD934X_ANA_AMIC4,
					      mask, val);
		break;
	default:
		break;
	}
}

static int wcd934x_codec_enable_adc(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd934x_codec_set_tx_hold(comp, w->reg, true);
		break;
	default:
		break;
	}

	return 0;
}

static int wcd934x_codec_enable_micbias(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd934x_micbias_control(component, micb_num, MICB_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 1 msec delay as per HW requirement */
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd934x_micbias_control(component, micb_num, MICB_DISABLE, true);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget wcd934x_dapm_widgets[] = {
	/* Analog Outputs */
	SND_SOC_DAPM_OUTPUT("EAR"),
	SND_SOC_DAPM_OUTPUT("HPHL"),
	SND_SOC_DAPM_OUTPUT("HPHR"),
	SND_SOC_DAPM_OUTPUT("LINEOUT1"),
	SND_SOC_DAPM_OUTPUT("LINEOUT2"),
	SND_SOC_DAPM_OUTPUT("SPK1 OUT"),
	SND_SOC_DAPM_OUTPUT("SPK2 OUT"),
	SND_SOC_DAPM_OUTPUT("ANC EAR"),
	SND_SOC_DAPM_OUTPUT("ANC HPHL"),
	SND_SOC_DAPM_OUTPUT("ANC HPHR"),
	SND_SOC_DAPM_OUTPUT("WDMA3_OUT"),
	SND_SOC_DAPM_OUTPUT("MAD_CPE_OUT1"),
	SND_SOC_DAPM_OUTPUT("MAD_CPE_OUT2"),
	SND_SOC_DAPM_AIF_IN_E("AIF1 PB", "AIF1 Playback", 0, SND_SOC_NOPM,
			      AIF1_PB, 0, wcd934x_codec_enable_slim,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AIF2 PB", "AIF2 Playback", 0, SND_SOC_NOPM,
			      AIF2_PB, 0, wcd934x_codec_enable_slim,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AIF3 PB", "AIF3 Playback", 0, SND_SOC_NOPM,
			      AIF3_PB, 0, wcd934x_codec_enable_slim,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AIF4 PB", "AIF4 Playback", 0, SND_SOC_NOPM,
			      AIF4_PB, 0, wcd934x_codec_enable_slim,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("SLIM RX0 MUX", SND_SOC_NOPM, WCD934X_RX0, 0,
			 &slim_rx_mux[WCD934X_RX0]),
	SND_SOC_DAPM_MUX("SLIM RX1 MUX", SND_SOC_NOPM, WCD934X_RX1, 0,
			 &slim_rx_mux[WCD934X_RX1]),
	SND_SOC_DAPM_MUX("SLIM RX2 MUX", SND_SOC_NOPM, WCD934X_RX2, 0,
			 &slim_rx_mux[WCD934X_RX2]),
	SND_SOC_DAPM_MUX("SLIM RX3 MUX", SND_SOC_NOPM, WCD934X_RX3, 0,
			 &slim_rx_mux[WCD934X_RX3]),
	SND_SOC_DAPM_MUX("SLIM RX4 MUX", SND_SOC_NOPM, WCD934X_RX4, 0,
			 &slim_rx_mux[WCD934X_RX4]),
	SND_SOC_DAPM_MUX("SLIM RX5 MUX", SND_SOC_NOPM, WCD934X_RX5, 0,
			 &slim_rx_mux[WCD934X_RX5]),
	SND_SOC_DAPM_MUX("SLIM RX6 MUX", SND_SOC_NOPM, WCD934X_RX6, 0,
			 &slim_rx_mux[WCD934X_RX6]),
	SND_SOC_DAPM_MUX("SLIM RX7 MUX", SND_SOC_NOPM, WCD934X_RX7, 0,
			 &slim_rx_mux[WCD934X_RX7]),

	SND_SOC_DAPM_MIXER("SLIM RX0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX6", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX7", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX_E("RX INT0_2 MUX", SND_SOC_NOPM, INTERP_EAR, 0,
			   &rx_int0_2_mux, wcd934x_codec_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT1_2 MUX", SND_SOC_NOPM, INTERP_HPHL, 0,
			   &rx_int1_2_mux, wcd934x_codec_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT2_2 MUX", SND_SOC_NOPM, INTERP_HPHR, 0,
			   &rx_int2_2_mux, wcd934x_codec_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT3_2 MUX", SND_SOC_NOPM, INTERP_LO1, 0,
			   &rx_int3_2_mux, wcd934x_codec_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT4_2 MUX", SND_SOC_NOPM, INTERP_LO2, 0,
			   &rx_int4_2_mux, wcd934x_codec_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT7_2 MUX", SND_SOC_NOPM, INTERP_SPKR1, 0,
			   &rx_int7_2_mux, wcd934x_codec_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT8_2 MUX", SND_SOC_NOPM, INTERP_SPKR2, 0,
			   &rx_int8_2_mux, wcd934x_codec_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("RX INT0_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
			 &rx_int0_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT0_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx_int0_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT0_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx_int0_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT1_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
			 &rx_int1_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT1_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx_int1_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT1_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx_int1_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT2_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
			 &rx_int2_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT2_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx_int2_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT2_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx_int2_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT3_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
			 &rx_int3_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT3_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx_int3_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT3_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx_int3_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT4_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
			 &rx_int4_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT4_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx_int4_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT4_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx_int4_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT7_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
			   &rx_int7_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT7_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			   &rx_int7_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT7_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			   &rx_int7_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT8_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
			   &rx_int8_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT8_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			   &rx_int8_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT8_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			   &rx_int8_1_mix_inp2_mux),
	SND_SOC_DAPM_MIXER("RX INT0_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT0 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1 SEC MIX", SND_SOC_NOPM, 0, 0,
			   rx_int1_asrc_switch,
			   ARRAY_SIZE(rx_int1_asrc_switch)),
	SND_SOC_DAPM_MIXER("RX INT2_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2 SEC MIX", SND_SOC_NOPM, 0, 0,
			   rx_int2_asrc_switch,
			   ARRAY_SIZE(rx_int2_asrc_switch)),
	SND_SOC_DAPM_MIXER("RX INT3_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT3 SEC MIX", SND_SOC_NOPM, 0, 0,
			   rx_int3_asrc_switch,
			   ARRAY_SIZE(rx_int3_asrc_switch)),
	SND_SOC_DAPM_MIXER("RX INT4_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT4 SEC MIX", SND_SOC_NOPM, 0, 0,
			   rx_int4_asrc_switch,
			   ARRAY_SIZE(rx_int4_asrc_switch)),
	SND_SOC_DAPM_MIXER("RX INT7_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT7 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT8_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT8 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT0 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1 MIX3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2 MIX3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT3 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT3 MIX3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT4 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT4 MIX3", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("RX INT7 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("RX INT7 CHAIN", SND_SOC_NOPM, 0, 0,
			     NULL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("RX INT8 CHAIN", SND_SOC_NOPM, 0, 0,
			     NULL, 0, NULL, 0),
	SND_SOC_DAPM_MUX_E("RX INT0 MIX2 INP", WCD934X_CDC_RX0_RX_PATH_CFG0, 4,
			   0,  &rx_int0_mix2_inp_mux, NULL,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT1 MIX2 INP", WCD934X_CDC_RX1_RX_PATH_CFG0, 4,
			   0, &rx_int1_mix2_inp_mux,  NULL,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT2 MIX2 INP", WCD934X_CDC_RX2_RX_PATH_CFG0, 4,
			   0, &rx_int2_mix2_inp_mux, NULL,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT3 MIX2 INP", WCD934X_CDC_RX3_RX_PATH_CFG0, 4,
			   0, &rx_int3_mix2_inp_mux, NULL,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT4 MIX2 INP", WCD934X_CDC_RX4_RX_PATH_CFG0, 4,
			   0, &rx_int4_mix2_inp_mux, NULL,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT7 MIX2 INP", WCD934X_CDC_RX7_RX_PATH_CFG0, 4,
			   0, &rx_int7_mix2_inp_mux, NULL,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("IIR0 INP0 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp0_mux),
	SND_SOC_DAPM_MUX("IIR0 INP1 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp1_mux),
	SND_SOC_DAPM_MUX("IIR0 INP2 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp2_mux),
	SND_SOC_DAPM_MUX("IIR0 INP3 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp3_mux),
	SND_SOC_DAPM_MUX("IIR1 INP0 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp0_mux),
	SND_SOC_DAPM_MUX("IIR1 INP1 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp1_mux),
	SND_SOC_DAPM_MUX("IIR1 INP2 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp2_mux),
	SND_SOC_DAPM_MUX("IIR1 INP3 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp3_mux),

	SND_SOC_DAPM_PGA_E("IIR0", WCD934X_CDC_SIDETONE_IIR0_IIR_GAIN_B1_CTL,
			   0, 0, NULL, 0, wcd934x_codec_set_iir_gain,
			   SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("IIR1", WCD934X_CDC_SIDETONE_IIR1_IIR_GAIN_B1_CTL,
			   1, 0, NULL, 0, wcd934x_codec_set_iir_gain,
			   SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER("SRC0", WCD934X_CDC_SIDETONE_SRC0_ST_SRC_PATH_CTL,
			   4, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SRC1", WCD934X_CDC_SIDETONE_SRC1_ST_SRC_PATH_CTL,
			   4, 0, NULL, 0),
	SND_SOC_DAPM_MUX("RX INT0 DEM MUX", SND_SOC_NOPM, 0, 0,
			 &rx_int0_dem_inp_mux),
	SND_SOC_DAPM_MUX("RX INT1 DEM MUX", SND_SOC_NOPM, 0, 0,
			 &rx_int1_dem_inp_mux),
	SND_SOC_DAPM_MUX("RX INT2 DEM MUX", SND_SOC_NOPM, 0, 0,
			 &rx_int2_dem_inp_mux),

	SND_SOC_DAPM_MUX_E("RX INT0_1 INTERP", SND_SOC_NOPM, INTERP_EAR, 0,
			   &rx_int0_1_interp_mux,
			   wcd934x_codec_enable_main_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT1_1 INTERP", SND_SOC_NOPM, INTERP_HPHL, 0,
			   &rx_int1_1_interp_mux,
			   wcd934x_codec_enable_main_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT2_1 INTERP", SND_SOC_NOPM, INTERP_HPHR, 0,
			   &rx_int2_1_interp_mux,
			   wcd934x_codec_enable_main_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT3_1 INTERP", SND_SOC_NOPM, INTERP_LO1, 0,
			   &rx_int3_1_interp_mux,
			   wcd934x_codec_enable_main_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT4_1 INTERP", SND_SOC_NOPM, INTERP_LO2, 0,
			   &rx_int4_1_interp_mux,
			   wcd934x_codec_enable_main_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT7_1 INTERP", SND_SOC_NOPM, INTERP_SPKR1, 0,
			   &rx_int7_1_interp_mux,
			   wcd934x_codec_enable_main_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT8_1 INTERP", SND_SOC_NOPM, INTERP_SPKR2, 0,
			   &rx_int8_1_interp_mux,
			   wcd934x_codec_enable_main_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("RX INT0_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int0_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT1_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int1_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT2_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int2_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT3_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int3_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT4_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int4_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT7_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int7_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT8_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int8_2_interp_mux),
	SND_SOC_DAPM_DAC_E("RX INT0 DAC", NULL, SND_SOC_NOPM,
			   0, 0, wcd934x_codec_ear_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT1 DAC", NULL, WCD934X_ANA_HPH,
			   5, 0, wcd934x_codec_hphl_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT2 DAC", NULL, WCD934X_ANA_HPH,
			   4, 0, wcd934x_codec_hphr_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT3 DAC", NULL, SND_SOC_NOPM,
			   0, 0, wcd934x_codec_lineout_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT4 DAC", NULL, SND_SOC_NOPM,
			   0, 0, wcd934x_codec_lineout_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("EAR PA", WCD934X_ANA_EAR, 7, 0, NULL, 0, NULL, 0),
	SND_SOC_DAPM_PGA_E("HPHL PA", WCD934X_ANA_HPH, 7, 0, NULL, 0,
			   wcd934x_codec_enable_hphl_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHR PA", WCD934X_ANA_HPH, 6, 0, NULL, 0,
			   wcd934x_codec_enable_hphr_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LINEOUT1 PA", WCD934X_ANA_LO_1_2, 7, 0, NULL, 0,
			   NULL, 0),
	SND_SOC_DAPM_PGA_E("LINEOUT2 PA", WCD934X_ANA_LO_1_2, 6, 0, NULL, 0,
			   NULL, 0),
	SND_SOC_DAPM_SUPPLY("RX_BIAS", WCD934X_ANA_RX_SUPPLIES, 0, 0, NULL,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("SBOOST0", WCD934X_CDC_RX7_RX_PATH_CFG1,
			 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SBOOST0_CLK", WCD934X_CDC_BOOST0_BOOST_PATH_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SBOOST1", WCD934X_CDC_RX8_RX_PATH_CFG1,
			 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SBOOST1_CLK", WCD934X_CDC_BOOST1_BOOST_PATH_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("INT0_CLK", SND_SOC_NOPM, INTERP_EAR, 0,
			    wcd934x_codec_enable_interp_clk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("INT1_CLK", SND_SOC_NOPM, INTERP_HPHL, 0,
			    wcd934x_codec_enable_interp_clk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("INT2_CLK", SND_SOC_NOPM, INTERP_HPHR, 0,
			    wcd934x_codec_enable_interp_clk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("INT3_CLK", SND_SOC_NOPM, INTERP_LO1, 0,
			    wcd934x_codec_enable_interp_clk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("INT4_CLK", SND_SOC_NOPM, INTERP_LO2, 0,
			    wcd934x_codec_enable_interp_clk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("INT7_CLK", SND_SOC_NOPM, INTERP_SPKR1, 0,
			    wcd934x_codec_enable_interp_clk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("INT8_CLK", SND_SOC_NOPM, INTERP_SPKR2, 0,
			    wcd934x_codec_enable_interp_clk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("DSMDEM0_CLK", WCD934X_CDC_RX0_RX_PATH_DSMDEM_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSMDEM1_CLK", WCD934X_CDC_RX1_RX_PATH_DSMDEM_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSMDEM2_CLK", WCD934X_CDC_RX2_RX_PATH_DSMDEM_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSMDEM3_CLK", WCD934X_CDC_RX3_RX_PATH_DSMDEM_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSMDEM4_CLK", WCD934X_CDC_RX4_RX_PATH_DSMDEM_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSMDEM7_CLK", WCD934X_CDC_RX7_RX_PATH_DSMDEM_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSMDEM8_CLK", WCD934X_CDC_RX8_RX_PATH_DSMDEM_CTL,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MCLK", SND_SOC_NOPM, 0, 0,
			    wcd934x_codec_enable_mclk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* TX */
	SND_SOC_DAPM_INPUT("AMIC1"),
	SND_SOC_DAPM_INPUT("AMIC2"),
	SND_SOC_DAPM_INPUT("AMIC3"),
	SND_SOC_DAPM_INPUT("AMIC4"),
	SND_SOC_DAPM_INPUT("AMIC5"),
	SND_SOC_DAPM_INPUT("DMIC0 Pin"),
	SND_SOC_DAPM_INPUT("DMIC1 Pin"),
	SND_SOC_DAPM_INPUT("DMIC2 Pin"),
	SND_SOC_DAPM_INPUT("DMIC3 Pin"),
	SND_SOC_DAPM_INPUT("DMIC4 Pin"),
	SND_SOC_DAPM_INPUT("DMIC5 Pin"),

	SND_SOC_DAPM_AIF_OUT_E("AIF1 CAP", "AIF1 Capture", 0, SND_SOC_NOPM,
			       AIF1_CAP, 0, wcd934x_codec_enable_slim,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT_E("AIF2 CAP", "AIF2 Capture", 0, SND_SOC_NOPM,
			       AIF2_CAP, 0, wcd934x_codec_enable_slim,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT_E("AIF3 CAP", "AIF3 Capture", 0, SND_SOC_NOPM,
			       AIF3_CAP, 0, wcd934x_codec_enable_slim,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("SLIM TX0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX6", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX7", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX8", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX9", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX10", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX11", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM TX13", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Mic Inputs */
	SND_SOC_DAPM_ADC_E("DMIC0", NULL, SND_SOC_NOPM, 0, 0,
			   wcd934x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd934x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC2", NULL, SND_SOC_NOPM, 0, 0,
			   wcd934x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC3", NULL, SND_SOC_NOPM, 0, 0,
			   wcd934x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC4", NULL, SND_SOC_NOPM, 0, 0,
			   wcd934x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC5", NULL, SND_SOC_NOPM, 0, 0,
			   wcd934x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX("DMIC MUX0", SND_SOC_NOPM, 0, 0, &tx_dmic_mux0),
	SND_SOC_DAPM_MUX("DMIC MUX1", SND_SOC_NOPM, 0, 0, &tx_dmic_mux1),
	SND_SOC_DAPM_MUX("DMIC MUX2", SND_SOC_NOPM, 0, 0, &tx_dmic_mux2),
	SND_SOC_DAPM_MUX("DMIC MUX3", SND_SOC_NOPM, 0, 0, &tx_dmic_mux3),
	SND_SOC_DAPM_MUX("DMIC MUX4", SND_SOC_NOPM, 0, 0, &tx_dmic_mux4),
	SND_SOC_DAPM_MUX("DMIC MUX5", SND_SOC_NOPM, 0, 0, &tx_dmic_mux5),
	SND_SOC_DAPM_MUX("DMIC MUX6", SND_SOC_NOPM, 0, 0, &tx_dmic_mux6),
	SND_SOC_DAPM_MUX("DMIC MUX7", SND_SOC_NOPM, 0, 0, &tx_dmic_mux7),
	SND_SOC_DAPM_MUX("DMIC MUX8", SND_SOC_NOPM, 0, 0, &tx_dmic_mux8),
	SND_SOC_DAPM_MUX("AMIC MUX0", SND_SOC_NOPM, 0, 0, &tx_amic_mux0),
	SND_SOC_DAPM_MUX("AMIC MUX1", SND_SOC_NOPM, 0, 0, &tx_amic_mux1),
	SND_SOC_DAPM_MUX("AMIC MUX2", SND_SOC_NOPM, 0, 0, &tx_amic_mux2),
	SND_SOC_DAPM_MUX("AMIC MUX3", SND_SOC_NOPM, 0, 0, &tx_amic_mux3),
	SND_SOC_DAPM_MUX("AMIC MUX4", SND_SOC_NOPM, 0, 0, &tx_amic_mux4),
	SND_SOC_DAPM_MUX("AMIC MUX5", SND_SOC_NOPM, 0, 0, &tx_amic_mux5),
	SND_SOC_DAPM_MUX("AMIC MUX6", SND_SOC_NOPM, 0, 0, &tx_amic_mux6),
	SND_SOC_DAPM_MUX("AMIC MUX7", SND_SOC_NOPM, 0, 0, &tx_amic_mux7),
	SND_SOC_DAPM_MUX("AMIC MUX8", SND_SOC_NOPM, 0, 0, &tx_amic_mux8),
	SND_SOC_DAPM_MUX_E("ADC MUX0", WCD934X_CDC_TX0_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux0_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX1", WCD934X_CDC_TX1_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux1_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX2", WCD934X_CDC_TX2_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux2_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX3", WCD934X_CDC_TX3_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux3_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX4", WCD934X_CDC_TX4_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux4_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX5", WCD934X_CDC_TX5_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux5_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX6", WCD934X_CDC_TX6_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux6_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX7", WCD934X_CDC_TX7_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux7_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("ADC MUX8", WCD934X_CDC_TX8_TX_PATH_CTL, 5, 0,
			   &tx_adc_mux8_mux, wcd934x_codec_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC1", NULL, WCD934X_ANA_AMIC1, 7, 0,
			   wcd934x_codec_enable_adc, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_ADC_E("ADC2", NULL, WCD934X_ANA_AMIC2, 7, 0,
			   wcd934x_codec_enable_adc, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_ADC_E("ADC3", NULL, WCD934X_ANA_AMIC3, 7, 0,
			   wcd934x_codec_enable_adc, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_ADC_E("ADC4", NULL, WCD934X_ANA_AMIC4, 7, 0,
			   wcd934x_codec_enable_adc, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
			    wcd934x_codec_enable_micbias, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
			    wcd934x_codec_enable_micbias, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
			    wcd934x_codec_enable_micbias, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS4", SND_SOC_NOPM, MIC_BIAS_4, 0,
			    wcd934x_codec_enable_micbias, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("AMIC4_5 SEL", SND_SOC_NOPM, 0, 0, &tx_amic4_5),
	SND_SOC_DAPM_MUX("CDC_IF TX0 MUX", SND_SOC_NOPM, WCD934X_TX0, 0,
			 &cdc_if_tx0_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX1 MUX", SND_SOC_NOPM, WCD934X_TX1, 0,
			 &cdc_if_tx1_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX2 MUX", SND_SOC_NOPM, WCD934X_TX2, 0,
			 &cdc_if_tx2_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX3 MUX", SND_SOC_NOPM, WCD934X_TX3, 0,
			 &cdc_if_tx3_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX4 MUX", SND_SOC_NOPM, WCD934X_TX4, 0,
			 &cdc_if_tx4_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX5 MUX", SND_SOC_NOPM, WCD934X_TX5, 0,
			 &cdc_if_tx5_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX6 MUX", SND_SOC_NOPM, WCD934X_TX6, 0,
			 &cdc_if_tx6_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX7 MUX", SND_SOC_NOPM, WCD934X_TX7, 0,
			 &cdc_if_tx7_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX8 MUX", SND_SOC_NOPM, WCD934X_TX8, 0,
			 &cdc_if_tx8_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX9 MUX", SND_SOC_NOPM, WCD934X_TX9, 0,
			 &cdc_if_tx9_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX10 MUX", SND_SOC_NOPM, WCD934X_TX10, 0,
			 &cdc_if_tx10_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX11 MUX", SND_SOC_NOPM, WCD934X_TX11, 0,
			 &cdc_if_tx11_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX11 INP1 MUX", SND_SOC_NOPM, WCD934X_TX11, 0,
			 &cdc_if_tx11_inp1_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX13 MUX", SND_SOC_NOPM, WCD934X_TX13, 0,
			 &cdc_if_tx13_mux),
	SND_SOC_DAPM_MUX("CDC_IF TX13 INP1 MUX", SND_SOC_NOPM, WCD934X_TX13, 0,
			 &cdc_if_tx13_inp1_mux),
	SND_SOC_DAPM_MIXER("AIF1_CAP Mixer", SND_SOC_NOPM, AIF1_CAP, 0,
			   aif1_slim_cap_mixer,
			   ARRAY_SIZE(aif1_slim_cap_mixer)),
	SND_SOC_DAPM_MIXER("AIF2_CAP Mixer", SND_SOC_NOPM, AIF2_CAP, 0,
			   aif2_slim_cap_mixer,
			   ARRAY_SIZE(aif2_slim_cap_mixer)),
	SND_SOC_DAPM_MIXER("AIF3_CAP Mixer", SND_SOC_NOPM, AIF3_CAP, 0,
			   aif3_slim_cap_mixer,
			   ARRAY_SIZE(aif3_slim_cap_mixer)),
};

static const struct snd_soc_dapm_route wcd934x_audio_map[] = {
	/* RX0-RX7 */
	WCD934X_SLIM_RX_AIF_PATH(0),
	WCD934X_SLIM_RX_AIF_PATH(1),
	WCD934X_SLIM_RX_AIF_PATH(2),
	WCD934X_SLIM_RX_AIF_PATH(3),
	WCD934X_SLIM_RX_AIF_PATH(4),
	WCD934X_SLIM_RX_AIF_PATH(5),
	WCD934X_SLIM_RX_AIF_PATH(6),
	WCD934X_SLIM_RX_AIF_PATH(7),

	/* RX0 Ear out */
	WCD934X_INTERPOLATOR_PATH(0),
	WCD934X_INTERPOLATOR_MIX2(0),
	{"RX INT0 DEM MUX", "CLSH_DSM_OUT", "RX INT0 MIX2"},
	{"RX INT0 DAC", NULL, "RX INT0 DEM MUX"},
	{"RX INT0 DAC", NULL, "RX_BIAS"},
	{"EAR PA", NULL, "RX INT0 DAC"},
	{"EAR", NULL, "EAR PA"},

	/* RX1 Headphone left */
	WCD934X_INTERPOLATOR_PATH(1),
	WCD934X_INTERPOLATOR_MIX2(1),
	{"RX INT1 MIX3", NULL, "RX INT1 MIX2"},
	{"RX INT1 DEM MUX", "CLSH_DSM_OUT", "RX INT1 MIX3"},
	{"RX INT1 DAC", NULL, "RX INT1 DEM MUX"},
	{"RX INT1 DAC", NULL, "RX_BIAS"},
	{"HPHL PA", NULL, "RX INT1 DAC"},
	{"HPHL", NULL, "HPHL PA"},

	/* RX2 Headphone right */
	WCD934X_INTERPOLATOR_PATH(2),
	WCD934X_INTERPOLATOR_MIX2(2),
	{"RX INT2 MIX3", NULL, "RX INT2 MIX2"},
	{"RX INT2 DEM MUX", "CLSH_DSM_OUT", "RX INT2 MIX3"},
	{"RX INT2 DAC", NULL, "RX INT2 DEM MUX"},
	{"RX INT2 DAC", NULL, "RX_BIAS"},
	{"HPHR PA", NULL, "RX INT2 DAC"},
	{"HPHR", NULL, "HPHR PA"},

	/* RX3 HIFi LineOut1 */
	WCD934X_INTERPOLATOR_PATH(3),
	WCD934X_INTERPOLATOR_MIX2(3),
	{"RX INT3 MIX3", NULL, "RX INT3 MIX2"},
	{"RX INT3 DAC", NULL, "RX INT3 MIX3"},
	{"RX INT3 DAC", NULL, "RX_BIAS"},
	{"LINEOUT1 PA", NULL, "RX INT3 DAC"},
	{"LINEOUT1", NULL, "LINEOUT1 PA"},

	/* RX4 HIFi LineOut2 */
	WCD934X_INTERPOLATOR_PATH(4),
	WCD934X_INTERPOLATOR_MIX2(4),
	{"RX INT4 MIX3", NULL, "RX INT4 MIX2"},
	{"RX INT4 DAC", NULL, "RX INT4 MIX3"},
	{"RX INT4 DAC", NULL, "RX_BIAS"},
	{"LINEOUT2 PA", NULL, "RX INT4 DAC"},
	{"LINEOUT2", NULL, "LINEOUT2 PA"},

	/* RX7 Speaker Left Out PA */
	WCD934X_INTERPOLATOR_PATH(7),
	WCD934X_INTERPOLATOR_MIX2(7),
	{"RX INT7 CHAIN", NULL, "RX INT7 MIX2"},
	{"RX INT7 CHAIN", NULL, "RX_BIAS"},
	{"RX INT7 CHAIN", NULL, "SBOOST0"},
	{"RX INT7 CHAIN", NULL, "SBOOST0_CLK"},
	{"SPK1 OUT", NULL, "RX INT7 CHAIN"},

	/* RX8 Speaker Right Out PA */
	WCD934X_INTERPOLATOR_PATH(8),
	{"RX INT8 CHAIN", NULL, "RX INT8 SEC MIX"},
	{"RX INT8 CHAIN", NULL, "RX_BIAS"},
	{"RX INT8 CHAIN", NULL, "SBOOST1"},
	{"RX INT8 CHAIN", NULL, "SBOOST1_CLK"},
	{"SPK2 OUT", NULL, "RX INT8 CHAIN"},

	/* Tx */
	{"AIF1 CAP", NULL, "AIF1_CAP Mixer"},
	{"AIF2 CAP", NULL, "AIF2_CAP Mixer"},
	{"AIF3 CAP", NULL, "AIF3_CAP Mixer"},

	WCD934X_SLIM_TX_AIF_PATH(0),
	WCD934X_SLIM_TX_AIF_PATH(1),
	WCD934X_SLIM_TX_AIF_PATH(2),
	WCD934X_SLIM_TX_AIF_PATH(3),
	WCD934X_SLIM_TX_AIF_PATH(4),
	WCD934X_SLIM_TX_AIF_PATH(5),
	WCD934X_SLIM_TX_AIF_PATH(6),
	WCD934X_SLIM_TX_AIF_PATH(7),
	WCD934X_SLIM_TX_AIF_PATH(8),

	WCD934X_ADC_MUX(0),
	WCD934X_ADC_MUX(1),
	WCD934X_ADC_MUX(2),
	WCD934X_ADC_MUX(3),
	WCD934X_ADC_MUX(4),
	WCD934X_ADC_MUX(5),
	WCD934X_ADC_MUX(6),
	WCD934X_ADC_MUX(7),
	WCD934X_ADC_MUX(8),

	{"CDC_IF TX0 MUX", "DEC0", "ADC MUX0"},
	{"CDC_IF TX1 MUX", "DEC1", "ADC MUX1"},
	{"CDC_IF TX2 MUX", "DEC2", "ADC MUX2"},
	{"CDC_IF TX3 MUX", "DEC3", "ADC MUX3"},
	{"CDC_IF TX4 MUX", "DEC4", "ADC MUX4"},
	{"CDC_IF TX5 MUX", "DEC5", "ADC MUX5"},
	{"CDC_IF TX6 MUX", "DEC6", "ADC MUX6"},
	{"CDC_IF TX7 MUX", "DEC7", "ADC MUX7"},
	{"CDC_IF TX8 MUX", "DEC8", "ADC MUX8"},

	{"AMIC4_5 SEL", "AMIC4", "AMIC4"},
	{"AMIC4_5 SEL", "AMIC5", "AMIC5"},

	{ "DMIC0", NULL, "DMIC0 Pin" },
	{ "DMIC1", NULL, "DMIC1 Pin" },
	{ "DMIC2", NULL, "DMIC2 Pin" },
	{ "DMIC3", NULL, "DMIC3 Pin" },
	{ "DMIC4", NULL, "DMIC4 Pin" },
	{ "DMIC5", NULL, "DMIC5 Pin" },

	{"ADC1", NULL, "AMIC1"},
	{"ADC2", NULL, "AMIC2"},
	{"ADC3", NULL, "AMIC3"},
	{"ADC4", NULL, "AMIC4_5 SEL"},

	WCD934X_IIR_INP_MUX(0),
	WCD934X_IIR_INP_MUX(1),

	{"SRC0", NULL, "IIR0"},
	{"SRC1", NULL, "IIR1"},
};

static int wcd934x_codec_set_jack(struct snd_soc_component *comp,
				  struct snd_soc_jack *jack, void *data)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	int ret = 0;

	if (!wcd->mbhc)
		return -ENOTSUPP;

	if (jack && !wcd->mbhc_started) {
		ret = wcd_mbhc_start(wcd->mbhc, &wcd->mbhc_cfg, jack);
		wcd->mbhc_started = true;
	} else if (wcd->mbhc_started) {
		wcd_mbhc_stop(wcd->mbhc);
		wcd->mbhc_started = false;
	}

	return ret;
}

static const struct snd_soc_component_driver wcd934x_component_drv = {
	.probe = wcd934x_comp_probe,
	.remove = wcd934x_comp_remove,
	.set_sysclk = wcd934x_comp_set_sysclk,
	.controls = wcd934x_snd_controls,
	.num_controls = ARRAY_SIZE(wcd934x_snd_controls),
	.dapm_widgets = wcd934x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wcd934x_dapm_widgets),
	.dapm_routes = wcd934x_audio_map,
	.num_dapm_routes = ARRAY_SIZE(wcd934x_audio_map),
	.set_jack = wcd934x_codec_set_jack,
	.endianness = 1,
};

static int wcd934x_codec_parse_data(struct wcd934x_codec *wcd)
{
	struct device *dev = &wcd->sdev->dev;
	struct wcd_mbhc_config *cfg = &wcd->mbhc_cfg;
	struct device_node *ifc_dev_np;

	ifc_dev_np = of_parse_phandle(dev->of_node, "slim-ifc-dev", 0);
	if (!ifc_dev_np)
		return dev_err_probe(dev, -EINVAL, "No Interface device found\n");

	wcd->sidev = of_slim_get_device(wcd->sdev->ctrl, ifc_dev_np);
	of_node_put(ifc_dev_np);
	if (!wcd->sidev)
		return dev_err_probe(dev, -EINVAL, "Unable to get SLIM Interface device\n");

	slim_get_logical_addr(wcd->sidev);
	wcd->if_regmap = regmap_init_slimbus(wcd->sidev,
				  &wcd934x_ifc_regmap_config);
	if (IS_ERR(wcd->if_regmap))
		return dev_err_probe(dev, PTR_ERR(wcd->if_regmap),
				     "Failed to allocate ifc register map\n");

	of_property_read_u32(dev->parent->of_node, "qcom,dmic-sample-rate",
			     &wcd->dmic_sample_rate);

	cfg->mbhc_micbias = MIC_BIAS_2;
	cfg->anc_micbias = MIC_BIAS_2;
	cfg->v_hs_max = WCD_MBHC_HS_V_MAX;
	cfg->num_btn = WCD934X_MBHC_MAX_BUTTONS;
	cfg->micb_mv = wcd->micb2_mv;
	cfg->linein_th = 5000;
	cfg->hs_thr = 1700;
	cfg->hph_thr = 50;

	wcd_dt_parse_mbhc_data(dev, cfg);


	return 0;
}

static int wcd934x_codec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wcd934x_ddata *data = dev_get_drvdata(dev->parent);
	struct wcd934x_codec *wcd;
	int ret, irq;

	wcd = devm_kzalloc(dev, sizeof(*wcd), GFP_KERNEL);
	if (!wcd)
		return -ENOMEM;

	wcd->dev = dev;
	wcd->regmap = data->regmap;
	wcd->extclk = data->extclk;
	wcd->sdev = to_slim_device(data->dev);
	mutex_init(&wcd->sysclk_mutex);
	mutex_init(&wcd->micb_lock);

	ret = wcd934x_codec_parse_data(wcd);
	if (ret)
		return ret;

	/* set default rate 9P6MHz */
	regmap_update_bits(wcd->regmap, WCD934X_CODEC_RPM_CLK_MCLK_CFG,
			   WCD934X_CODEC_RPM_CLK_MCLK_CFG_MCLK_MASK,
			   WCD934X_CODEC_RPM_CLK_MCLK_CFG_9P6MHZ);
	memcpy(wcd->rx_chs, wcd934x_rx_chs, sizeof(wcd934x_rx_chs));
	memcpy(wcd->tx_chs, wcd934x_tx_chs, sizeof(wcd934x_tx_chs));

	irq = regmap_irq_get_virq(data->irq_data, WCD934X_IRQ_SLIMBUS);
	if (irq < 0)
		return dev_err_probe(wcd->dev, irq, "Failed to get SLIM IRQ\n");

	ret = devm_request_threaded_irq(dev, irq, NULL,
					wcd934x_slim_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"slim", wcd);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request slimbus irq\n");

	wcd934x_register_mclk_output(wcd);
	platform_set_drvdata(pdev, wcd);

	return devm_snd_soc_register_component(dev, &wcd934x_component_drv,
					       wcd934x_slim_dais,
					       ARRAY_SIZE(wcd934x_slim_dais));
}

static const struct platform_device_id wcd934x_driver_id[] = {
	{
		.name = "wcd934x-codec",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, wcd934x_driver_id);

static struct platform_driver wcd934x_codec_driver = {
	.probe	= &wcd934x_codec_probe,
	.id_table = wcd934x_driver_id,
	.driver = {
		.name	= "wcd934x-codec",
	}
};

module_platform_driver(wcd934x_codec_driver);
MODULE_DESCRIPTION("WCD934x codec driver");
MODULE_LICENSE("GPL v2");
