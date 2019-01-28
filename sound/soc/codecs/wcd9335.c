// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
// Copyright (c) 2017-2018, Linaro Limited

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slimbus.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <sound/tlv.h>
#include <sound/info.h>
#include "wcd9335.h"
#include "wcd-clsh-v2.h"

#define WCD9335_RATES_MASK (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			    SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			    SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
/* Fractional Rates */
#define WCD9335_FRAC_RATES_MASK (SNDRV_PCM_RATE_44100)
#define WCD9335_FORMATS_S16_S24_LE (SNDRV_PCM_FMTBIT_S16_LE | \
				  SNDRV_PCM_FMTBIT_S24_LE)

/* slave port water mark level
 *   (0: 6bytes, 1: 9bytes, 2: 12 bytes, 3: 15 bytes)
 */
#define SLAVE_PORT_WATER_MARK_6BYTES  0
#define SLAVE_PORT_WATER_MARK_9BYTES  1
#define SLAVE_PORT_WATER_MARK_12BYTES 2
#define SLAVE_PORT_WATER_MARK_15BYTES 3
#define SLAVE_PORT_WATER_MARK_SHIFT 1
#define SLAVE_PORT_ENABLE           1
#define SLAVE_PORT_DISABLE          0
#define WCD9335_SLIM_WATER_MARK_VAL \
	((SLAVE_PORT_WATER_MARK_12BYTES << SLAVE_PORT_WATER_MARK_SHIFT) | \
	 (SLAVE_PORT_ENABLE))

#define WCD9335_SLIM_NUM_PORT_REG 3
#define WCD9335_SLIM_PGD_PORT_INT_TX_EN0 (WCD9335_SLIM_PGD_PORT_INT_EN0 + 2)

#define WCD9335_MCLK_CLK_12P288MHZ	12288000
#define WCD9335_MCLK_CLK_9P6MHZ		9600000

#define WCD9335_SLIM_CLOSE_TIMEOUT 1000
#define WCD9335_SLIM_IRQ_OVERFLOW (1 << 0)
#define WCD9335_SLIM_IRQ_UNDERFLOW (1 << 1)
#define WCD9335_SLIM_IRQ_PORT_CLOSED (1 << 2)

#define WCD9335_NUM_INTERPOLATORS 9
#define WCD9335_RX_START	16
#define WCD9335_SLIM_CH_START 128

#define WCD9335_SLIM_RX_CH(p) \
	{.port = p + WCD9335_RX_START, .shift = p,}

/* vout step value */
#define WCD9335_CALCULATE_VOUT_D(req_mv) (((req_mv - 650) * 10) / 25)

enum {
	WCD9335_RX0 = 0,
	WCD9335_RX1,
	WCD9335_RX2,
	WCD9335_RX3,
	WCD9335_RX4,
	WCD9335_RX5,
	WCD9335_RX6,
	WCD9335_RX7,
	WCD9335_RX8,
	WCD9335_RX9,
	WCD9335_RX10,
	WCD9335_RX11,
	WCD9335_RX12,
	WCD9335_RX_MAX,
};

enum {
	SIDO_SOURCE_INTERNAL = 0,
	SIDO_SOURCE_RCO_BG,
};

enum wcd9335_sido_voltage {
	SIDO_VOLTAGE_SVS_MV = 950,
	SIDO_VOLTAGE_NOMINAL_MV = 1100,
};

enum {
	AIF1_PB = 0,
	AIF1_CAP,
	AIF2_PB,
	AIF2_CAP,
	AIF3_PB,
	AIF3_CAP,
	AIF4_PB,
	NUM_CODEC_DAIS,
};

enum {
	COMPANDER_1, /* HPH_L */
	COMPANDER_2, /* HPH_R */
	COMPANDER_3, /* LO1_DIFF */
	COMPANDER_4, /* LO2_DIFF */
	COMPANDER_5, /* LO3_SE */
	COMPANDER_6, /* LO4_SE */
	COMPANDER_7, /* SWR SPK CH1 */
	COMPANDER_8, /* SWR SPK CH2 */
	COMPANDER_MAX,
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

enum {
	INTn_1_MIX_INP_SEL_ZERO = 0,
	INTn_1_MIX_INP_SEL_DEC0,
	INTn_1_MIX_INP_SEL_DEC1,
	INTn_1_MIX_INP_SEL_IIR0,
	INTn_1_MIX_INP_SEL_IIR1,
	INTn_1_MIX_INP_SEL_RX0,
	INTn_1_MIX_INP_SEL_RX1,
	INTn_1_MIX_INP_SEL_RX2,
	INTn_1_MIX_INP_SEL_RX3,
	INTn_1_MIX_INP_SEL_RX4,
	INTn_1_MIX_INP_SEL_RX5,
	INTn_1_MIX_INP_SEL_RX6,
	INTn_1_MIX_INP_SEL_RX7,

};

enum {
	INTERP_EAR = 0,
	INTERP_HPHL,
	INTERP_HPHR,
	INTERP_LO1,
	INTERP_LO2,
	INTERP_LO3,
	INTERP_LO4,
	INTERP_SPKR1,
	INTERP_SPKR2,
};

enum wcd_clock_type {
	WCD_CLK_OFF,
	WCD_CLK_RCO,
	WCD_CLK_MCLK,
};

struct wcd9335_slim_ch {
	u32 ch_num;
	u16 port;
	u16 shift;
	struct list_head list;
};

struct wcd_slim_codec_dai_data {
	struct list_head slim_ch_list;
	struct slim_stream_config sconfig;
	struct slim_stream_runtime *sruntime;
};

struct wcd9335_codec {
	struct device *dev;
	struct clk *mclk;
	struct clk *native_clk;
	u32 mclk_rate;
	u8 version;

	struct slim_device *slim;
	struct slim_device *slim_ifc_dev;
	struct regmap *regmap;
	struct regmap *if_regmap;
	struct regmap_irq_chip_data *irq_data;

	struct wcd9335_slim_ch rx_chs[WCD9335_RX_MAX];
	u32 num_rx_port;

	int sido_input_src;
	enum wcd9335_sido_voltage sido_voltage;

	struct wcd_slim_codec_dai_data dai[NUM_CODEC_DAIS];
	struct snd_soc_component *component;

	int master_bias_users;
	int clk_mclk_users;
	int clk_rco_users;
	int sido_ccl_cnt;
	enum wcd_clock_type clk_type;

	struct wcd_clsh_ctrl *clsh_ctrl;
	u32 hph_mode;
	int prim_int_users[WCD9335_NUM_INTERPOLATORS];

	int comp_enabled[COMPANDER_MAX];

	int intr1;
	int reset_gpio;
	struct regulator_bulk_data supplies[WCD9335_MAX_SUPPLY];

	unsigned int rx_port_value;
	int hph_l_gain;
	int hph_r_gain;
	u32 rx_bias_count;

};

struct wcd9335_irq {
	int irq;
	irqreturn_t (*handler)(int irq, void *data);
	char *name;
};

static const struct wcd9335_slim_ch wcd9335_rx_chs[WCD9335_RX_MAX] = {
	WCD9335_SLIM_RX_CH(0),	 /* 16 */
	WCD9335_SLIM_RX_CH(1),	 /* 17 */
	WCD9335_SLIM_RX_CH(2),
	WCD9335_SLIM_RX_CH(3),
	WCD9335_SLIM_RX_CH(4),
	WCD9335_SLIM_RX_CH(5),
	WCD9335_SLIM_RX_CH(6),
	WCD9335_SLIM_RX_CH(7),
	WCD9335_SLIM_RX_CH(8),
	WCD9335_SLIM_RX_CH(9),
	WCD9335_SLIM_RX_CH(10),
	WCD9335_SLIM_RX_CH(11),
	WCD9335_SLIM_RX_CH(12),
};

struct interp_sample_rate {
	int rate;
	int rate_val;
};

static struct interp_sample_rate int_mix_rate_val[] = {
	{48000, 0x4},	/* 48K */
	{96000, 0x5},	/* 96K */
	{192000, 0x6},	/* 192K */
};

static struct interp_sample_rate int_prim_rate_val[] = {
	{8000, 0x0},	/* 8K */
	{16000, 0x1},	/* 16K */
	{24000, -EINVAL},/* 24K */
	{32000, 0x3},	/* 32K */
	{48000, 0x4},	/* 48K */
	{96000, 0x5},	/* 96K */
	{192000, 0x6},	/* 192K */
	{384000, 0x7},	/* 384K */
	{44100, 0x8}, /* 44.1K */
};

struct wcd9335_reg_mask_val {
	u16 reg;
	u8 mask;
	u8 val;
};

static const struct wcd9335_reg_mask_val wcd9335_codec_reg_init[] = {
	/* Rbuckfly/R_EAR(32) */
	{WCD9335_CDC_CLSH_K2_MSB, 0x0F, 0x00},
	{WCD9335_CDC_CLSH_K2_LSB, 0xFF, 0x60},
	{WCD9335_CPE_SS_DMIC_CFG, 0x80, 0x00},
	{WCD9335_CDC_BOOST0_BOOST_CTL, 0x70, 0x50},
	{WCD9335_CDC_BOOST1_BOOST_CTL, 0x70, 0x50},
	{WCD9335_CDC_RX7_RX_PATH_CFG1, 0x08, 0x08},
	{WCD9335_CDC_RX8_RX_PATH_CFG1, 0x08, 0x08},
	{WCD9335_ANA_LO_1_2, 0x3C, 0X3C},
	{WCD9335_DIFF_LO_COM_SWCAP_REFBUF_FREQ, 0x70, 0x00},
	{WCD9335_DIFF_LO_COM_PA_FREQ, 0x70, 0x40},
	{WCD9335_SOC_MAD_AUDIO_CTL_2, 0x03, 0x03},
	{WCD9335_CDC_TOP_TOP_CFG1, 0x02, 0x02},
	{WCD9335_CDC_TOP_TOP_CFG1, 0x01, 0x01},
	{WCD9335_EAR_CMBUFF, 0x08, 0x00},
	{WCD9335_CDC_TX9_SPKR_PROT_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_TX10_SPKR_PROT_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_TX11_SPKR_PROT_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_TX12_SPKR_PROT_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_COMPANDER7_CTL3, 0x80, 0x80},
	{WCD9335_CDC_COMPANDER8_CTL3, 0x80, 0x80},
	{WCD9335_CDC_COMPANDER7_CTL7, 0x01, 0x01},
	{WCD9335_CDC_COMPANDER8_CTL7, 0x01, 0x01},
	{WCD9335_CDC_RX0_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX1_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX2_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX3_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX4_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX5_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX6_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX7_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX8_RX_PATH_CFG0, 0x01, 0x01},
	{WCD9335_CDC_RX0_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX1_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX2_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX3_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX4_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX5_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX6_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX7_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_CDC_RX8_RX_PATH_MIX_CFG, 0x01, 0x01},
	{WCD9335_VBADC_IBIAS_FE, 0x0C, 0x08},
	{WCD9335_RCO_CTRL_2, 0x0F, 0x08},
	{WCD9335_RX_BIAS_FLYB_MID_RST, 0xF0, 0x10},
	{WCD9335_FLYBACK_CTRL_1, 0x20, 0x20},
	{WCD9335_HPH_OCP_CTL, 0xFF, 0x5A},
	{WCD9335_HPH_L_TEST, 0x01, 0x01},
	{WCD9335_HPH_R_TEST, 0x01, 0x01},
	{WCD9335_CDC_BOOST0_BOOST_CFG1, 0x3F, 0x12},
	{WCD9335_CDC_BOOST0_BOOST_CFG2, 0x1C, 0x08},
	{WCD9335_CDC_COMPANDER7_CTL7, 0x1E, 0x18},
	{WCD9335_CDC_BOOST1_BOOST_CFG1, 0x3F, 0x12},
	{WCD9335_CDC_BOOST1_BOOST_CFG2, 0x1C, 0x08},
	{WCD9335_CDC_COMPANDER8_CTL7, 0x1E, 0x18},
	{WCD9335_CDC_TX0_TX_PATH_SEC7, 0xFF, 0x45},
	{WCD9335_CDC_RX0_RX_PATH_SEC0, 0xFC, 0xF4},
	{WCD9335_HPH_REFBUFF_LP_CTL, 0x08, 0x08},
	{WCD9335_HPH_REFBUFF_LP_CTL, 0x06, 0x02},
};

/* Cutoff frequency for high pass filter */
static const char * const cf_text[] = {
	"CF_NEG_3DB_4HZ", "CF_NEG_3DB_75HZ", "CF_NEG_3DB_150HZ"
};

static const char * const rx_cf_text[] = {
	"CF_NEG_3DB_4HZ", "CF_NEG_3DB_75HZ", "CF_NEG_3DB_150HZ",
	"CF_NEG_3DB_0P48HZ"
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

static const char * const rx_int_dem_inp_mux_text[] = {
	"NORMAL_DSM_OUT", "CLSH_DSM_OUT",
};

static const char * const rx_int0_interp_mux_text[] = {
	"ZERO", "RX INT0 MIX2",
};

static const char * const rx_int1_interp_mux_text[] = {
	"ZERO", "RX INT1 MIX2",
};

static const char * const rx_int2_interp_mux_text[] = {
	"ZERO", "RX INT2 MIX2",
};

static const char * const rx_int3_interp_mux_text[] = {
	"ZERO", "RX INT3 MIX2",
};

static const char * const rx_int4_interp_mux_text[] = {
	"ZERO", "RX INT4 MIX2",
};

static const char * const rx_int5_interp_mux_text[] = {
	"ZERO", "RX INT5 MIX2",
};

static const char * const rx_int6_interp_mux_text[] = {
	"ZERO", "RX INT6 MIX2",
};

static const char * const rx_int7_interp_mux_text[] = {
	"ZERO", "RX INT7 MIX2",
};

static const char * const rx_int8_interp_mux_text[] = {
	"ZERO", "RX INT8 SEC MIX"
};

static const char * const rx_hph_mode_mux_text[] = {
	"Class H Invalid", "Class-H Hi-Fi", "Class-H Low Power", "Class-AB",
	"Class-H Hi-Fi Low Power"
};

static const char *const slim_rx_mux_text[] = {
	"ZERO", "AIF1_PB", "AIF2_PB", "AIF3_PB", "AIF4_PB",
};

static const DECLARE_TLV_DB_SCALE(digital_gain, 0, 1, 0);
static const DECLARE_TLV_DB_SCALE(line_gain, 0, 7, 1);
static const DECLARE_TLV_DB_SCALE(analog_gain, 0, 25, 1);
static const DECLARE_TLV_DB_SCALE(ear_pa_gain, 0, 150, 0);

static const struct soc_enum cf_dec0_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX0_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX1_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec2_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX2_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec3_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX3_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec4_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX4_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec5_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX5_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec6_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX6_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec7_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX7_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_dec8_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_TX8_TX_PATH_CFG0, 5, 3, cf_text);

static const struct soc_enum cf_int0_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX0_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int0_2_enum, WCD9335_CDC_RX0_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int1_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX1_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int1_2_enum, WCD9335_CDC_RX1_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int2_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX2_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int2_2_enum, WCD9335_CDC_RX2_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int3_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX3_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int3_2_enum, WCD9335_CDC_RX3_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int4_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX4_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int4_2_enum, WCD9335_CDC_RX4_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int5_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX5_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int5_2_enum, WCD9335_CDC_RX5_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int6_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX6_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int6_2_enum, WCD9335_CDC_RX6_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int7_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX7_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int7_2_enum, WCD9335_CDC_RX7_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum cf_int8_1_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX8_RX_PATH_CFG2, 0, 4, rx_cf_text);

static SOC_ENUM_SINGLE_DECL(cf_int8_2_enum, WCD9335_CDC_RX8_RX_PATH_MIX_CFG, 2,
		     rx_cf_text);

static const struct soc_enum rx_hph_mode_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_hph_mode_mux_text),
			    rx_hph_mode_mux_text);

static const struct soc_enum slim_rx_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(slim_rx_mux_text), slim_rx_mux_text);

static const struct soc_enum rx_int0_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT0_CFG1, 0, 10,
			rx_int0_7_mix_mux_text);

static const struct soc_enum rx_int1_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT1_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int2_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT2_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int3_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT3_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int4_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT4_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int5_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT5_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int6_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT6_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int7_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT7_CFG1, 0, 10,
			rx_int0_7_mix_mux_text);

static const struct soc_enum rx_int8_2_mux_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT8_CFG1, 0, 9,
			rx_int_mix_mux_text);

static const struct soc_enum rx_int0_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT0_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int0_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT0_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int0_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT0_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int1_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT1_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int1_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT1_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int1_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT1_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int2_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT2_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int2_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT2_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int2_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT2_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int3_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT3_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int3_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT3_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int3_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT3_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int4_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT4_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int4_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT4_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int4_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT4_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int5_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT5_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int5_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT5_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int5_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT5_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int6_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT6_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int6_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT6_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int6_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT6_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int7_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT7_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int7_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT7_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int7_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT7_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int8_1_mix_inp0_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT8_CFG0, 0, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int8_1_mix_inp1_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT8_CFG0, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int8_1_mix_inp2_chain_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX_INP_MUX_RX_INT8_CFG1, 4, 13,
			rx_prim_mix_text);

static const struct soc_enum rx_int0_dem_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX0_RX_PATH_SEC0, 0,
			ARRAY_SIZE(rx_int_dem_inp_mux_text),
			rx_int_dem_inp_mux_text);

static const struct soc_enum rx_int1_dem_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX1_RX_PATH_SEC0, 0,
			ARRAY_SIZE(rx_int_dem_inp_mux_text),
			rx_int_dem_inp_mux_text);

static const struct soc_enum rx_int2_dem_inp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX2_RX_PATH_SEC0, 0,
			ARRAY_SIZE(rx_int_dem_inp_mux_text),
			rx_int_dem_inp_mux_text);

static const struct soc_enum rx_int0_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX0_RX_PATH_CTL, 5, 2,
			rx_int0_interp_mux_text);

static const struct soc_enum rx_int1_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX1_RX_PATH_CTL, 5, 2,
			rx_int1_interp_mux_text);

static const struct soc_enum rx_int2_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX2_RX_PATH_CTL, 5, 2,
			rx_int2_interp_mux_text);

static const struct soc_enum rx_int3_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX3_RX_PATH_CTL, 5, 2,
			rx_int3_interp_mux_text);

static const struct soc_enum rx_int4_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX4_RX_PATH_CTL, 5, 2,
			rx_int4_interp_mux_text);

static const struct soc_enum rx_int5_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX5_RX_PATH_CTL, 5, 2,
			rx_int5_interp_mux_text);

static const struct soc_enum rx_int6_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX6_RX_PATH_CTL, 5, 2,
			rx_int6_interp_mux_text);

static const struct soc_enum rx_int7_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX7_RX_PATH_CTL, 5, 2,
			rx_int7_interp_mux_text);

static const struct soc_enum rx_int8_interp_mux_enum =
	SOC_ENUM_SINGLE(WCD9335_CDC_RX8_RX_PATH_CTL, 5, 2,
			rx_int8_interp_mux_text);

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

static const struct snd_kcontrol_new rx_int5_2_mux =
	SOC_DAPM_ENUM("RX INT5_2 MUX Mux", rx_int5_2_mux_chain_enum);

static const struct snd_kcontrol_new rx_int6_2_mux =
	SOC_DAPM_ENUM("RX INT6_2 MUX Mux", rx_int6_2_mux_chain_enum);

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

static const struct snd_kcontrol_new rx_int5_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT5_1 MIX1 INP0 Mux", rx_int5_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int5_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT5_1 MIX1 INP1 Mux", rx_int5_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int5_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT5_1 MIX1 INP2 Mux", rx_int5_1_mix_inp2_chain_enum);

static const struct snd_kcontrol_new rx_int6_1_mix_inp0_mux =
	SOC_DAPM_ENUM("RX INT6_1 MIX1 INP0 Mux", rx_int6_1_mix_inp0_chain_enum);

static const struct snd_kcontrol_new rx_int6_1_mix_inp1_mux =
	SOC_DAPM_ENUM("RX INT6_1 MIX1 INP1 Mux", rx_int6_1_mix_inp1_chain_enum);

static const struct snd_kcontrol_new rx_int6_1_mix_inp2_mux =
	SOC_DAPM_ENUM("RX INT6_1 MIX1 INP2 Mux", rx_int6_1_mix_inp2_chain_enum);

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

static const struct snd_kcontrol_new rx_int0_interp_mux =
	SOC_DAPM_ENUM("RX INT0 INTERP Mux", rx_int0_interp_mux_enum);

static const struct snd_kcontrol_new rx_int1_interp_mux =
	SOC_DAPM_ENUM("RX INT1 INTERP Mux", rx_int1_interp_mux_enum);

static const struct snd_kcontrol_new rx_int2_interp_mux =
	SOC_DAPM_ENUM("RX INT2 INTERP Mux", rx_int2_interp_mux_enum);

static const struct snd_kcontrol_new rx_int3_interp_mux =
	SOC_DAPM_ENUM("RX INT3 INTERP Mux", rx_int3_interp_mux_enum);

static const struct snd_kcontrol_new rx_int4_interp_mux =
	SOC_DAPM_ENUM("RX INT4 INTERP Mux", rx_int4_interp_mux_enum);

static const struct snd_kcontrol_new rx_int5_interp_mux =
	SOC_DAPM_ENUM("RX INT5 INTERP Mux", rx_int5_interp_mux_enum);

static const struct snd_kcontrol_new rx_int6_interp_mux =
	SOC_DAPM_ENUM("RX INT6 INTERP Mux", rx_int6_interp_mux_enum);

static const struct snd_kcontrol_new rx_int7_interp_mux =
	SOC_DAPM_ENUM("RX INT7 INTERP Mux", rx_int7_interp_mux_enum);

static const struct snd_kcontrol_new rx_int8_interp_mux =
	SOC_DAPM_ENUM("RX INT8 INTERP Mux", rx_int8_interp_mux_enum);

static int slim_rx_mux_get(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kc);
	struct wcd9335_codec *wcd = dev_get_drvdata(dapm->dev);

	ucontrol->value.enumerated.item[0] = wcd->rx_port_value;

	return 0;
}

static int slim_rx_mux_put(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_soc_dapm_kcontrol_widget(kc);
	struct wcd9335_codec *wcd = dev_get_drvdata(w->dapm->dev);
	struct soc_enum *e = (struct soc_enum *)kc->private_value;
	struct snd_soc_dapm_update *update = NULL;
	u32 port_id = w->shift;

	wcd->rx_port_value = ucontrol->value.enumerated.item[0];

	switch (wcd->rx_port_value) {
	case 0:
		list_del_init(&wcd->rx_chs[port_id].list);
		break;
	case 1:
		list_add_tail(&wcd->rx_chs[port_id].list,
			      &wcd->dai[AIF1_PB].slim_ch_list);
		break;
	case 2:
		list_add_tail(&wcd->rx_chs[port_id].list,
			      &wcd->dai[AIF2_PB].slim_ch_list);
		break;
	case 3:
		list_add_tail(&wcd->rx_chs[port_id].list,
			      &wcd->dai[AIF3_PB].slim_ch_list);
		break;
	case 4:
		list_add_tail(&wcd->rx_chs[port_id].list,
			      &wcd->dai[AIF4_PB].slim_ch_list);
		break;
	default:
		dev_err(wcd->dev, "Unknown AIF %d\n", wcd->rx_port_value);
		goto err;
	}

	snd_soc_dapm_mux_update_power(w->dapm, kc, wcd->rx_port_value,
				      e, update);

	return 0;
err:
	return -EINVAL;
}

static const struct snd_kcontrol_new slim_rx_mux[WCD9335_RX_MAX] = {
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

static int wcd9335_int_dem_inp_mux_put(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *)kc->private_value;
	struct snd_soc_component *component;
	int reg, val;

	component = snd_soc_dapm_kcontrol_component(kc);
	val = ucontrol->value.enumerated.item[0];

	if (e->reg == WCD9335_CDC_RX0_RX_PATH_SEC0)
		reg = WCD9335_CDC_RX0_RX_PATH_CFG0;
	else if (e->reg == WCD9335_CDC_RX1_RX_PATH_SEC0)
		reg = WCD9335_CDC_RX1_RX_PATH_CFG0;
	else if (e->reg == WCD9335_CDC_RX2_RX_PATH_SEC0)
		reg = WCD9335_CDC_RX2_RX_PATH_CFG0;
	else
		return -EINVAL;

	/* Set Look Ahead Delay */
	snd_soc_component_update_bits(component, reg,
				WCD9335_CDC_RX_PATH_CFG0_DLY_ZN_EN_MASK,
				val ? WCD9335_CDC_RX_PATH_CFG0_DLY_ZN_EN : 0);
	/* Set DEM INP Select */
	return snd_soc_dapm_put_enum_double(kc, ucontrol);
}

static const struct snd_kcontrol_new rx_int0_dem_inp_mux =
	SOC_DAPM_ENUM_EXT("RX INT0 DEM MUX Mux", rx_int0_dem_inp_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  wcd9335_int_dem_inp_mux_put);

static const struct snd_kcontrol_new rx_int1_dem_inp_mux =
	SOC_DAPM_ENUM_EXT("RX INT1 DEM MUX Mux", rx_int1_dem_inp_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  wcd9335_int_dem_inp_mux_put);

static const struct snd_kcontrol_new rx_int2_dem_inp_mux =
	SOC_DAPM_ENUM_EXT("RX INT2 DEM MUX Mux", rx_int2_dem_inp_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  wcd9335_int_dem_inp_mux_put);

static int wcd9335_set_mix_interpolator_rate(struct snd_soc_dai *dai,
					     int rate_val,
					     u32 rate)
{
	struct snd_soc_component *component = dai->component;
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	struct wcd9335_slim_ch *ch;
	int val, j;

	list_for_each_entry(ch, &wcd->dai[dai->id].slim_ch_list, list) {
		for (j = 0; j < WCD9335_NUM_INTERPOLATORS; j++) {
			val = snd_soc_component_read32(component,
					WCD9335_CDC_RX_INP_MUX_RX_INT_CFG1(j)) &
					WCD9335_CDC_RX_INP_MUX_RX_INT_SEL_MASK;

			if (val == (ch->shift + INTn_2_INP_SEL_RX0))
				snd_soc_component_update_bits(component,
						WCD9335_CDC_RX_PATH_MIX_CTL(j),
						WCD9335_CDC_MIX_PCM_RATE_MASK,
						rate_val);
		}
	}

	return 0;
}

static int wcd9335_set_prim_interpolator_rate(struct snd_soc_dai *dai,
					      u8 rate_val,
					      u32 rate)
{
	struct snd_soc_component *comp = dai->component;
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);
	struct wcd9335_slim_ch *ch;
	u8 cfg0, cfg1, inp0_sel, inp1_sel, inp2_sel;
	int inp, j;

	list_for_each_entry(ch, &wcd->dai[dai->id].slim_ch_list, list) {
		inp = ch->shift + INTn_1_MIX_INP_SEL_RX0;
		/*
		 * Loop through all interpolator MUX inputs and find out
		 * to which interpolator input, the slim rx port
		 * is connected
		 */
		for (j = 0; j < WCD9335_NUM_INTERPOLATORS; j++) {
			cfg0 = snd_soc_component_read32(comp,
					WCD9335_CDC_RX_INP_MUX_RX_INT_CFG0(j));
			cfg1 = snd_soc_component_read32(comp,
					WCD9335_CDC_RX_INP_MUX_RX_INT_CFG1(j));

			inp0_sel = cfg0 &
				 WCD9335_CDC_RX_INP_MUX_RX_INT_SEL_MASK;
			inp1_sel = (cfg0 >> 4) &
				 WCD9335_CDC_RX_INP_MUX_RX_INT_SEL_MASK;
			inp2_sel = (cfg1 >> 4) &
				 WCD9335_CDC_RX_INP_MUX_RX_INT_SEL_MASK;

			if ((inp0_sel == inp) ||  (inp1_sel == inp) ||
			    (inp2_sel == inp)) {
				/* rate is in Hz */
				if ((j == 0) && (rate == 44100))
					dev_info(wcd->dev,
						"Cannot set 44.1KHz on INT0\n");
				else
					snd_soc_component_update_bits(comp,
						WCD9335_CDC_RX_PATH_CTL(j),
						WCD9335_CDC_MIX_PCM_RATE_MASK,
						rate_val);
			}
		}
	}

	return 0;
}

static int wcd9335_set_interpolator_rate(struct snd_soc_dai *dai, u32 rate)
{
	int i;

	/* set mixing path rate */
	for (i = 0; i < ARRAY_SIZE(int_mix_rate_val); i++) {
		if (rate == int_mix_rate_val[i].rate) {
			wcd9335_set_mix_interpolator_rate(dai,
					int_mix_rate_val[i].rate_val, rate);
			break;
		}
	}

	/* set primary path sample rate */
	for (i = 0; i < ARRAY_SIZE(int_prim_rate_val); i++) {
		if (rate == int_prim_rate_val[i].rate) {
			wcd9335_set_prim_interpolator_rate(dai,
					int_prim_rate_val[i].rate_val, rate);
			break;
		}
	}

	return 0;
}

static int wcd9335_slim_set_hw_params(struct wcd9335_codec *wcd,
				 struct wcd_slim_codec_dai_data *dai_data,
				 int direction)
{
	struct list_head *slim_ch_list = &dai_data->slim_ch_list;
	struct slim_stream_config *cfg = &dai_data->sconfig;
	struct wcd9335_slim_ch *ch;
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
				WCD9335_SLIM_PGD_RX_PORT_MULTI_CHNL_0(ch->port),
				payload);

			if (ret < 0)
				goto err;

			/* configure the slave port for water mark and enable*/
			ret = regmap_write(wcd->if_regmap,
					WCD9335_SLIM_PGD_RX_PORT_CFG(ch->port),
					WCD9335_SLIM_WATER_MARK_VAL);
			if (ret < 0)
				goto err;
		}
	}

	dai_data->sruntime = slim_stream_allocate(wcd->slim, "WCD9335-SLIM");

	return 0;

err:
	dev_err(wcd->dev, "Error Setting slim hw params\n");
	kfree(cfg->chs);
	cfg->chs = NULL;

	return ret;
}

static int wcd9335_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct wcd9335_codec *wcd;
	int ret;

	wcd = snd_soc_component_get_drvdata(dai->component);

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		ret = wcd9335_set_interpolator_rate(dai, params_rate(params));
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
			dev_err(wcd->dev, "%s: Invalid format 0x%x\n",
				__func__, params_width(params));
			return -EINVAL;
		}
		break;
	default:
		dev_err(wcd->dev, "Invalid stream type %d\n",
			substream->stream);
		return -EINVAL;
	};

	wcd->dai[dai->id].sconfig.rate = params_rate(params);
	wcd9335_slim_set_hw_params(wcd, &wcd->dai[dai->id], substream->stream);

	return 0;
}

static int wcd9335_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct wcd_slim_codec_dai_data *dai_data;
	struct wcd9335_codec *wcd;
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
		slim_stream_unprepare(dai_data->sruntime);
		slim_stream_disable(dai_data->sruntime);
		break;
	default:
		break;
	}

	return 0;
}

static int wcd9335_set_channel_map(struct snd_soc_dai *dai,
				   unsigned int tx_num, unsigned int *tx_slot,
				   unsigned int rx_num, unsigned int *rx_slot)
{
	struct wcd9335_codec *wcd;
	int i;

	wcd = snd_soc_component_get_drvdata(dai->component);

	if (!tx_slot || !rx_slot) {
		dev_err(wcd->dev, "Invalid tx_slot=%p, rx_slot=%p\n",
			tx_slot, rx_slot);
		return -EINVAL;
	}

	if (wcd->rx_chs) {
		wcd->num_rx_port = rx_num;
		for (i = 0; i < rx_num; i++) {
			wcd->rx_chs[i].ch_num = rx_slot[i];
			INIT_LIST_HEAD(&wcd->rx_chs[i].list);
		}
	}

	return 0;
}

static int wcd9335_get_channel_map(struct snd_soc_dai *dai,
				   unsigned int *tx_num, unsigned int *tx_slot,
				   unsigned int *rx_num, unsigned int *rx_slot)
{
	struct wcd9335_slim_ch *ch;
	struct wcd9335_codec *wcd;
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
	default:
		dev_err(wcd->dev, "Invalid DAI ID %x\n", dai->id);
		break;
	}

	return 0;
}

static struct snd_soc_dai_ops wcd9335_dai_ops = {
	.hw_params = wcd9335_hw_params,
	.trigger = wcd9335_trigger,
	.set_channel_map = wcd9335_set_channel_map,
	.get_channel_map = wcd9335_get_channel_map,
};

static struct snd_soc_dai_driver wcd9335_slim_dais[] = {
	[0] = {
		.name = "wcd9335_rx1",
		.id = AIF1_PB,
		.playback = {
			.stream_name = "AIF1 Playback",
			.rates = WCD9335_RATES_MASK | WCD9335_FRAC_RATES_MASK,
			.formats = WCD9335_FORMATS_S16_S24_LE,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd9335_dai_ops,
	},
	[1] = {
		.name = "wcd9335_tx1",
		.id = AIF1_CAP,
		.capture = {
			.stream_name = "AIF1 Capture",
			.rates = WCD9335_RATES_MASK,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd9335_dai_ops,
	},
	[2] = {
		.name = "wcd9335_rx2",
		.id = AIF2_PB,
		.playback = {
			.stream_name = "AIF2 Playback",
			.rates = WCD9335_RATES_MASK | WCD9335_FRAC_RATES_MASK,
			.formats = WCD9335_FORMATS_S16_S24_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd9335_dai_ops,
	},
	[3] = {
		.name = "wcd9335_tx2",
		.id = AIF2_CAP,
		.capture = {
			.stream_name = "AIF2 Capture",
			.rates = WCD9335_RATES_MASK,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd9335_dai_ops,
	},
	[4] = {
		.name = "wcd9335_rx3",
		.id = AIF3_PB,
		.playback = {
			.stream_name = "AIF3 Playback",
			.rates = WCD9335_RATES_MASK | WCD9335_FRAC_RATES_MASK,
			.formats = WCD9335_FORMATS_S16_S24_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd9335_dai_ops,
	},
	[5] = {
		.name = "wcd9335_tx3",
		.id = AIF3_CAP,
		.capture = {
			.stream_name = "AIF3 Capture",
			.rates = WCD9335_RATES_MASK,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd9335_dai_ops,
	},
	[6] = {
		.name = "wcd9335_rx4",
		.id = AIF4_PB,
		.playback = {
			.stream_name = "AIF4 Playback",
			.rates = WCD9335_RATES_MASK | WCD9335_FRAC_RATES_MASK,
			.formats = WCD9335_FORMATS_S16_S24_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd9335_dai_ops,
	},
};

static int wcd9335_get_compander(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	int comp = ((struct soc_mixer_control *)kc->private_value)->shift;
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);

	ucontrol->value.integer.value[0] = wcd->comp_enabled[comp];
	return 0;
}

static int wcd9335_set_compander(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	int comp = ((struct soc_mixer_control *) kc->private_value)->shift;
	int value = ucontrol->value.integer.value[0];
	int sel;

	wcd->comp_enabled[comp] = value;
	sel = value ? WCD9335_HPH_GAIN_SRC_SEL_COMPANDER :
		WCD9335_HPH_GAIN_SRC_SEL_REGISTER;

	/* Any specific register configuration for compander */
	switch (comp) {
	case COMPANDER_1:
		/* Set Gain Source Select based on compander enable/disable */
		snd_soc_component_update_bits(component, WCD9335_HPH_L_EN,
				      WCD9335_HPH_GAIN_SRC_SEL_MASK, sel);
		break;
	case COMPANDER_2:
		snd_soc_component_update_bits(component, WCD9335_HPH_R_EN,
				      WCD9335_HPH_GAIN_SRC_SEL_MASK, sel);
		break;
	case COMPANDER_5:
		snd_soc_component_update_bits(component, WCD9335_SE_LO_LO3_GAIN,
				      WCD9335_HPH_GAIN_SRC_SEL_MASK, sel);
		break;
	case COMPANDER_6:
		snd_soc_component_update_bits(component, WCD9335_SE_LO_LO4_GAIN,
				      WCD9335_HPH_GAIN_SRC_SEL_MASK, sel);
		break;
	default:
		break;
	};

	return 0;
}

static int wcd9335_rx_hph_mode_get(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);

	ucontrol->value.enumerated.item[0] = wcd->hph_mode;

	return 0;
}

static int wcd9335_rx_hph_mode_put(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kc);
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	u32 mode_val;

	mode_val = ucontrol->value.enumerated.item[0];

	if (mode_val == 0) {
		dev_err(wcd->dev, "Invalid HPH Mode, default to ClSH HiFi\n");
		mode_val = CLS_H_HIFI;
	}
	wcd->hph_mode = mode_val;

	return 0;
}

static const struct snd_kcontrol_new wcd9335_snd_controls[] = {
	/* -84dB min - 40dB max */
	SOC_SINGLE_SX_TLV("RX0 Digital Volume", WCD9335_CDC_RX0_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX1 Digital Volume", WCD9335_CDC_RX1_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX2 Digital Volume", WCD9335_CDC_RX2_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX3 Digital Volume", WCD9335_CDC_RX3_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX4 Digital Volume", WCD9335_CDC_RX4_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX5 Digital Volume", WCD9335_CDC_RX5_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX6 Digital Volume", WCD9335_CDC_RX6_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX7 Digital Volume", WCD9335_CDC_RX7_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX8 Digital Volume", WCD9335_CDC_RX8_RX_VOL_CTL,
		0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX0 Mix Digital Volume",
			  WCD9335_CDC_RX0_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX1 Mix Digital Volume",
			  WCD9335_CDC_RX1_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX2 Mix Digital Volume",
			  WCD9335_CDC_RX2_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX3 Mix Digital Volume",
			  WCD9335_CDC_RX3_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX4 Mix Digital Volume",
			  WCD9335_CDC_RX4_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX5 Mix Digital Volume",
			  WCD9335_CDC_RX5_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX6 Mix Digital Volume",
			  WCD9335_CDC_RX6_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX7 Mix Digital Volume",
			  WCD9335_CDC_RX7_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("RX8 Mix Digital Volume",
			  WCD9335_CDC_RX8_RX_VOL_MIX_CTL,
			  0, -84, 40, digital_gain),
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
	SOC_ENUM("RX INT5_1 HPF cut off", cf_int5_1_enum),
	SOC_ENUM("RX INT5_2 HPF cut off", cf_int5_2_enum),
	SOC_ENUM("RX INT6_1 HPF cut off", cf_int6_1_enum),
	SOC_ENUM("RX INT6_2 HPF cut off", cf_int6_2_enum),
	SOC_ENUM("RX INT7_1 HPF cut off", cf_int7_1_enum),
	SOC_ENUM("RX INT7_2 HPF cut off", cf_int7_2_enum),
	SOC_ENUM("RX INT8_1 HPF cut off", cf_int8_1_enum),
	SOC_ENUM("RX INT8_2 HPF cut off", cf_int8_2_enum),
	SOC_SINGLE_EXT("COMP1 Switch", SND_SOC_NOPM, COMPANDER_1, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_SINGLE_EXT("COMP2 Switch", SND_SOC_NOPM, COMPANDER_2, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_SINGLE_EXT("COMP3 Switch", SND_SOC_NOPM, COMPANDER_3, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_SINGLE_EXT("COMP4 Switch", SND_SOC_NOPM, COMPANDER_4, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_SINGLE_EXT("COMP5 Switch", SND_SOC_NOPM, COMPANDER_5, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_SINGLE_EXT("COMP6 Switch", SND_SOC_NOPM, COMPANDER_6, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_SINGLE_EXT("COMP7 Switch", SND_SOC_NOPM, COMPANDER_7, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_SINGLE_EXT("COMP8 Switch", SND_SOC_NOPM, COMPANDER_8, 1, 0,
		       wcd9335_get_compander, wcd9335_set_compander),
	SOC_ENUM_EXT("RX HPH Mode", rx_hph_mode_mux_enum,
		       wcd9335_rx_hph_mode_get, wcd9335_rx_hph_mode_put),

	/* Gain Controls */
	SOC_SINGLE_TLV("EAR PA Volume", WCD9335_ANA_EAR, 4, 4, 1,
		ear_pa_gain),
	SOC_SINGLE_TLV("HPHL Volume", WCD9335_HPH_L_EN, 0, 20, 1,
		line_gain),
	SOC_SINGLE_TLV("HPHR Volume", WCD9335_HPH_R_EN, 0, 20, 1,
		line_gain),
	SOC_SINGLE_TLV("LINEOUT1 Volume", WCD9335_DIFF_LO_LO1_COMPANDER,
			3, 16, 1, line_gain),
	SOC_SINGLE_TLV("LINEOUT2 Volume", WCD9335_DIFF_LO_LO2_COMPANDER,
			3, 16, 1, line_gain),
	SOC_SINGLE_TLV("LINEOUT3 Volume", WCD9335_SE_LO_LO3_GAIN, 0, 20, 1,
			line_gain),
	SOC_SINGLE_TLV("LINEOUT4 Volume", WCD9335_SE_LO_LO4_GAIN, 0, 20, 1,
			line_gain),

	SOC_SINGLE_TLV("ADC1 Volume", WCD9335_ANA_AMIC1, 0, 20, 0,
			analog_gain),
	SOC_SINGLE_TLV("ADC2 Volume", WCD9335_ANA_AMIC2, 0, 20, 0,
			analog_gain),
	SOC_SINGLE_TLV("ADC3 Volume", WCD9335_ANA_AMIC3, 0, 20, 0,
			analog_gain),
	SOC_SINGLE_TLV("ADC4 Volume", WCD9335_ANA_AMIC4, 0, 20, 0,
			analog_gain),
	SOC_SINGLE_TLV("ADC5 Volume", WCD9335_ANA_AMIC5, 0, 20, 0,
			analog_gain),
	SOC_SINGLE_TLV("ADC6 Volume", WCD9335_ANA_AMIC6, 0, 20, 0,
			analog_gain),

	SOC_ENUM("TX0 HPF cut off", cf_dec0_enum),
	SOC_ENUM("TX1 HPF cut off", cf_dec1_enum),
	SOC_ENUM("TX2 HPF cut off", cf_dec2_enum),
	SOC_ENUM("TX3 HPF cut off", cf_dec3_enum),
	SOC_ENUM("TX4 HPF cut off", cf_dec4_enum),
	SOC_ENUM("TX5 HPF cut off", cf_dec5_enum),
	SOC_ENUM("TX6 HPF cut off", cf_dec6_enum),
	SOC_ENUM("TX7 HPF cut off", cf_dec7_enum),
	SOC_ENUM("TX8 HPF cut off", cf_dec8_enum),
};

static void wcd9335_codec_enable_int_port(struct wcd_slim_codec_dai_data *dai,
					struct snd_soc_component *component)
{
	int port_num = 0;
	unsigned short reg = 0;
	unsigned int val = 0;
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	struct wcd9335_slim_ch *ch;

	list_for_each_entry(ch, &dai->slim_ch_list, list) {
		if (ch->port >= WCD9335_RX_START) {
			port_num = ch->port - WCD9335_RX_START;
			reg = WCD9335_SLIM_PGD_PORT_INT_EN0 + (port_num / 8);
		} else {
			port_num = ch->port;
			reg = WCD9335_SLIM_PGD_PORT_INT_TX_EN0 + (port_num / 8);
		}

		regmap_read(wcd->if_regmap, reg, &val);
		if (!(val & BIT(port_num % 8)))
			regmap_write(wcd->if_regmap, reg,
					val | BIT(port_num % 8));
	}
}

static int wcd9335_codec_enable_slim(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kc,
				       int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = snd_soc_component_get_drvdata(comp);
	struct wcd_slim_codec_dai_data *dai = &wcd->dai[w->shift];
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		wcd9335_codec_enable_int_port(dai, comp);
		break;
	case SND_SOC_DAPM_POST_PMD:
		kfree(dai->sconfig.chs);

		break;
	}

	return ret;
}

static int wcd9335_codec_enable_mix_path(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	u16 gain_reg;
	int offset_val = 0;
	int val = 0;

	switch (w->reg) {
	case WCD9335_CDC_RX0_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX0_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX1_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX1_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX2_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX2_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX3_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX3_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX4_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX4_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX5_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX5_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX6_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX6_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX7_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX7_RX_VOL_MIX_CTL;
		break;
	case WCD9335_CDC_RX8_RX_PATH_MIX_CTL:
		gain_reg = WCD9335_CDC_RX8_RX_VOL_MIX_CTL;
		break;
	default:
		dev_err(comp->dev, "%s: No gain register avail for %s\n",
			__func__, w->name);
		return 0;
	};

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = snd_soc_component_read32(comp, gain_reg);
		val += offset_val;
		snd_soc_component_write(comp, gain_reg, val);
		break;
	case SND_SOC_DAPM_POST_PMD:
		break;
	};

	return 0;
}

static u16 wcd9335_interp_get_primary_reg(u16 reg, u16 *ind)
{
	u16 prim_int_reg = WCD9335_CDC_RX0_RX_PATH_CTL;

	switch (reg) {
	case WCD9335_CDC_RX0_RX_PATH_CTL:
	case WCD9335_CDC_RX0_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX0_RX_PATH_CTL;
		*ind = 0;
		break;
	case WCD9335_CDC_RX1_RX_PATH_CTL:
	case WCD9335_CDC_RX1_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX1_RX_PATH_CTL;
		*ind = 1;
		break;
	case WCD9335_CDC_RX2_RX_PATH_CTL:
	case WCD9335_CDC_RX2_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX2_RX_PATH_CTL;
		*ind = 2;
		break;
	case WCD9335_CDC_RX3_RX_PATH_CTL:
	case WCD9335_CDC_RX3_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX3_RX_PATH_CTL;
		*ind = 3;
		break;
	case WCD9335_CDC_RX4_RX_PATH_CTL:
	case WCD9335_CDC_RX4_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX4_RX_PATH_CTL;
		*ind = 4;
		break;
	case WCD9335_CDC_RX5_RX_PATH_CTL:
	case WCD9335_CDC_RX5_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX5_RX_PATH_CTL;
		*ind = 5;
		break;
	case WCD9335_CDC_RX6_RX_PATH_CTL:
	case WCD9335_CDC_RX6_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX6_RX_PATH_CTL;
		*ind = 6;
		break;
	case WCD9335_CDC_RX7_RX_PATH_CTL:
	case WCD9335_CDC_RX7_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX7_RX_PATH_CTL;
		*ind = 7;
		break;
	case WCD9335_CDC_RX8_RX_PATH_CTL:
	case WCD9335_CDC_RX8_RX_PATH_MIX_CTL:
		prim_int_reg = WCD9335_CDC_RX8_RX_PATH_CTL;
		*ind = 8;
		break;
	};

	return prim_int_reg;
}

static void wcd9335_codec_hd2_control(struct snd_soc_component *component,
				    u16 prim_int_reg, int event)
{
	u16 hd2_scale_reg;
	u16 hd2_enable_reg = 0;

	if (prim_int_reg == WCD9335_CDC_RX1_RX_PATH_CTL) {
		hd2_scale_reg = WCD9335_CDC_RX1_RX_PATH_SEC3;
		hd2_enable_reg = WCD9335_CDC_RX1_RX_PATH_CFG0;
	}
	if (prim_int_reg == WCD9335_CDC_RX2_RX_PATH_CTL) {
		hd2_scale_reg = WCD9335_CDC_RX2_RX_PATH_SEC3;
		hd2_enable_reg = WCD9335_CDC_RX2_RX_PATH_CFG0;
	}

	if (hd2_enable_reg && SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(component, hd2_scale_reg,
				WCD9335_CDC_RX_PATH_SEC_HD2_ALPHA_MASK,
				WCD9335_CDC_RX_PATH_SEC_HD2_ALPHA_0P2500);
		snd_soc_component_update_bits(component, hd2_scale_reg,
				WCD9335_CDC_RX_PATH_SEC_HD2_SCALE_MASK,
				WCD9335_CDC_RX_PATH_SEC_HD2_SCALE_2);
		snd_soc_component_update_bits(component, hd2_enable_reg,
				WCD9335_CDC_RX_PATH_CFG_HD2_EN_MASK,
				WCD9335_CDC_RX_PATH_CFG_HD2_ENABLE);
	}

	if (hd2_enable_reg && SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(component, hd2_enable_reg,
					WCD9335_CDC_RX_PATH_CFG_HD2_EN_MASK,
					WCD9335_CDC_RX_PATH_CFG_HD2_DISABLE);
		snd_soc_component_update_bits(component, hd2_scale_reg,
					WCD9335_CDC_RX_PATH_SEC_HD2_SCALE_MASK,
					WCD9335_CDC_RX_PATH_SEC_HD2_SCALE_1);
		snd_soc_component_update_bits(component, hd2_scale_reg,
				WCD9335_CDC_RX_PATH_SEC_HD2_ALPHA_MASK,
				WCD9335_CDC_RX_PATH_SEC_HD2_ALPHA_0P0000);
	}
}

static int wcd9335_codec_enable_prim_interpolator(
						struct snd_soc_component *comp,
						u16 reg, int event)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);
	u16 ind = 0;
	int prim_int_reg = wcd9335_interp_get_primary_reg(reg, &ind);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd->prim_int_users[ind]++;
		if (wcd->prim_int_users[ind] == 1) {
			snd_soc_component_update_bits(comp, prim_int_reg,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_ENABLE);
			wcd9335_codec_hd2_control(comp, prim_int_reg, event);
			snd_soc_component_update_bits(comp, prim_int_reg,
					WCD9335_CDC_RX_CLK_EN_MASK,
					WCD9335_CDC_RX_CLK_ENABLE);
		}

		if ((reg != prim_int_reg) &&
			((snd_soc_component_read32(comp, prim_int_reg)) &
			 WCD9335_CDC_RX_PGA_MUTE_EN_MASK))
			snd_soc_component_update_bits(comp, reg,
						WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
						WCD9335_CDC_RX_PGA_MUTE_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd->prim_int_users[ind]--;
		if (wcd->prim_int_users[ind] == 0) {
			snd_soc_component_update_bits(comp, prim_int_reg,
					WCD9335_CDC_RX_CLK_EN_MASK,
					WCD9335_CDC_RX_CLK_DISABLE);
			snd_soc_component_update_bits(comp, prim_int_reg,
					WCD9335_CDC_RX_RESET_MASK,
					WCD9335_CDC_RX_RESET_ENABLE);
			snd_soc_component_update_bits(comp, prim_int_reg,
					WCD9335_CDC_RX_RESET_MASK,
					WCD9335_CDC_RX_RESET_DISABLE);
			wcd9335_codec_hd2_control(comp, prim_int_reg, event);
		}
		break;
	};

	return 0;
}

static int wcd9335_config_compander(struct snd_soc_component *component,
				    int interp_n, int event)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	int comp;
	u16 comp_ctl0_reg, rx_path_cfg0_reg;

	/* EAR does not have compander */
	if (!interp_n)
		return 0;

	comp = interp_n - 1;
	if (!wcd->comp_enabled[comp])
		return 0;

	comp_ctl0_reg = WCD9335_CDC_COMPANDER1_CTL(comp);
	rx_path_cfg0_reg = WCD9335_CDC_RX1_RX_PATH_CFG(comp);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Enable Compander Clock */
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					WCD9335_CDC_COMPANDER_CLK_EN_MASK,
					WCD9335_CDC_COMPANDER_CLK_ENABLE);
		/* Reset comander */
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					WCD9335_CDC_COMPANDER_SOFT_RST_MASK,
					WCD9335_CDC_COMPANDER_SOFT_RST_ENABLE);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
				WCD9335_CDC_COMPANDER_SOFT_RST_MASK,
				WCD9335_CDC_COMPANDER_SOFT_RST_DISABLE);
		/* Enables DRE in this path */
		snd_soc_component_update_bits(component, rx_path_cfg0_reg,
					WCD9335_CDC_RX_PATH_CFG_CMP_EN_MASK,
					WCD9335_CDC_RX_PATH_CFG_CMP_ENABLE);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					WCD9335_CDC_COMPANDER_HALT_MASK,
					WCD9335_CDC_COMPANDER_HALT);
		snd_soc_component_update_bits(component, rx_path_cfg0_reg,
					WCD9335_CDC_RX_PATH_CFG_CMP_EN_MASK,
					WCD9335_CDC_RX_PATH_CFG_CMP_DISABLE);

		snd_soc_component_update_bits(component, comp_ctl0_reg,
					WCD9335_CDC_COMPANDER_SOFT_RST_MASK,
					WCD9335_CDC_COMPANDER_SOFT_RST_ENABLE);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
				WCD9335_CDC_COMPANDER_SOFT_RST_MASK,
				WCD9335_CDC_COMPANDER_SOFT_RST_DISABLE);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					WCD9335_CDC_COMPANDER_CLK_EN_MASK,
					WCD9335_CDC_COMPANDER_CLK_DISABLE);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					WCD9335_CDC_COMPANDER_HALT_MASK,
					WCD9335_CDC_COMPANDER_NOHALT);
	}

	return 0;
}

static int wcd9335_codec_enable_interpolator(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	u16 gain_reg;
	u16 reg;
	int val;
	int offset_val = 0;

	if (!(strcmp(w->name, "RX INT0 INTERP"))) {
		reg = WCD9335_CDC_RX0_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX0_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT1 INTERP"))) {
		reg = WCD9335_CDC_RX1_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX1_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT2 INTERP"))) {
		reg = WCD9335_CDC_RX2_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX2_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT3 INTERP"))) {
		reg = WCD9335_CDC_RX3_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX3_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT4 INTERP"))) {
		reg = WCD9335_CDC_RX4_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX4_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT5 INTERP"))) {
		reg = WCD9335_CDC_RX5_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX5_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT6 INTERP"))) {
		reg = WCD9335_CDC_RX6_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX6_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT7 INTERP"))) {
		reg = WCD9335_CDC_RX7_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX7_RX_VOL_CTL;
	} else if (!(strcmp(w->name, "RX INT8 INTERP"))) {
		reg = WCD9335_CDC_RX8_RX_PATH_CTL;
		gain_reg = WCD9335_CDC_RX8_RX_VOL_CTL;
	} else {
		dev_err(comp->dev, "%s: Interpolator reg not found\n",
			__func__);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Reset if needed */
		wcd9335_codec_enable_prim_interpolator(comp, reg, event);
		break;
	case SND_SOC_DAPM_POST_PMU:
		wcd9335_config_compander(comp, w->shift, event);
		val = snd_soc_component_read32(comp, gain_reg);
		val += offset_val;
		snd_soc_component_write(comp, gain_reg, val);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd9335_config_compander(comp, w->shift, event);
		wcd9335_codec_enable_prim_interpolator(comp, reg, event);
		break;
	};

	return 0;
}

static void wcd9335_codec_hph_mode_gain_opt(struct snd_soc_component *component,
					    u8 gain)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	u8 hph_l_en, hph_r_en;
	u8 l_val, r_val;
	u8 hph_pa_status;
	bool is_hphl_pa, is_hphr_pa;

	hph_pa_status = snd_soc_component_read32(component, WCD9335_ANA_HPH);
	is_hphl_pa = hph_pa_status >> 7;
	is_hphr_pa = (hph_pa_status & 0x40) >> 6;

	hph_l_en = snd_soc_component_read32(component, WCD9335_HPH_L_EN);
	hph_r_en = snd_soc_component_read32(component, WCD9335_HPH_R_EN);

	l_val = (hph_l_en & 0xC0) | 0x20 | gain;
	r_val = (hph_r_en & 0xC0) | 0x20 | gain;

	/*
	 * Set HPH_L & HPH_R gain source selection to REGISTER
	 * for better click and pop only if corresponding PAs are
	 * not enabled. Also cache the values of the HPHL/R
	 * PA gains to be applied after PAs are enabled
	 */
	if ((l_val != hph_l_en) && !is_hphl_pa) {
		snd_soc_component_write(component, WCD9335_HPH_L_EN, l_val);
		wcd->hph_l_gain = hph_l_en & 0x1F;
	}

	if ((r_val != hph_r_en) && !is_hphr_pa) {
		snd_soc_component_write(component, WCD9335_HPH_R_EN, r_val);
		wcd->hph_r_gain = hph_r_en & 0x1F;
	}
}

static void wcd9335_codec_hph_lohifi_config(struct snd_soc_component *comp,
					  int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(comp, WCD9335_RX_BIAS_HPH_PA,
					WCD9335_RX_BIAS_HPH_PA_AMP_5_UA_MASK,
					0x06);
		snd_soc_component_update_bits(comp,
					WCD9335_RX_BIAS_HPH_RDACBUFF_CNP2,
					0xF0, 0x40);
		snd_soc_component_update_bits(comp, WCD9335_HPH_CNP_WG_CTL,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_MASK,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_RATIO_1000);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
				WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_MASK,
				WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_ENABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL1,
				WCD9335_HPH_PA_GM3_IB_SCALE_MASK,
				0x0C);
		wcd9335_codec_hph_mode_gain_opt(comp, 0x11);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
			WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_MASK,
			WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_DISABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_CNP_WG_CTL,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_MASK,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_RATIO_500);
		snd_soc_component_write(comp, WCD9335_RX_BIAS_HPH_RDACBUFF_CNP2,
					0x8A);
		snd_soc_component_update_bits(comp, WCD9335_RX_BIAS_HPH_PA,
					WCD9335_RX_BIAS_HPH_PA_AMP_5_UA_MASK,
					0x0A);
	}
}

static void wcd9335_codec_hph_lp_config(struct snd_soc_component *comp,
				      int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL1,
				WCD9335_HPH_PA_GM3_IB_SCALE_MASK,
				0x0C);
		wcd9335_codec_hph_mode_gain_opt(comp, 0x10);
		snd_soc_component_update_bits(comp, WCD9335_HPH_CNP_WG_CTL,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_MASK,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_RATIO_1000);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
			WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_MASK,
			WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_ENABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
				WCD9335_HPH_PA_CTL2_FORCE_PSRREH_MASK,
				WCD9335_HPH_PA_CTL2_FORCE_PSRREH_ENABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
				WCD9335_HPH_PA_CTL2_HPH_PSRR_ENH_MASK,
				WCD9335_HPH_PA_CTL2_HPH_PSRR_ENABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_RDAC_LDO_CTL,
				WCD9335_HPH_RDAC_N1P65_LD_OUTCTL_MASK,
				WCD9335_HPH_RDAC_N1P65_LD_OUTCTL_V_N1P60);
		snd_soc_component_update_bits(comp, WCD9335_HPH_RDAC_LDO_CTL,
				WCD9335_HPH_RDAC_1P65_LD_OUTCTL_MASK,
				WCD9335_HPH_RDAC_1P65_LD_OUTCTL_V_N1P60);
		snd_soc_component_update_bits(comp,
				WCD9335_RX_BIAS_HPH_RDAC_LDO, 0x0F, 0x01);
		snd_soc_component_update_bits(comp,
				WCD9335_RX_BIAS_HPH_RDAC_LDO, 0xF0, 0x10);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_write(comp, WCD9335_RX_BIAS_HPH_RDAC_LDO,
					0x88);
		snd_soc_component_write(comp, WCD9335_HPH_RDAC_LDO_CTL,
					0x33);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
				WCD9335_HPH_PA_CTL2_HPH_PSRR_ENH_MASK,
				WCD9335_HPH_PA_CTL2_HPH_PSRR_DISABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
				WCD9335_HPH_PA_CTL2_FORCE_PSRREH_MASK,
				WCD9335_HPH_PA_CTL2_FORCE_PSRREH_DISABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
				WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_MASK,
				WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_DISABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_CNP_WG_CTL,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_MASK,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_RATIO_500);
		snd_soc_component_update_bits(comp, WCD9335_HPH_R_EN,
				WCD9335_HPH_CONST_SEL_L_MASK,
				WCD9335_HPH_CONST_SEL_L_HQ_PATH);
		snd_soc_component_update_bits(comp, WCD9335_HPH_L_EN,
				WCD9335_HPH_CONST_SEL_L_MASK,
				WCD9335_HPH_CONST_SEL_L_HQ_PATH);
	}
}

static void wcd9335_codec_hph_hifi_config(struct snd_soc_component *comp,
					int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(comp, WCD9335_HPH_CNP_WG_CTL,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_MASK,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_RATIO_1000);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
				WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_MASK,
				WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_ENABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL1,
				WCD9335_HPH_PA_GM3_IB_SCALE_MASK,
				0x0C);
		wcd9335_codec_hph_mode_gain_opt(comp, 0x11);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(comp, WCD9335_HPH_PA_CTL2,
			WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_MASK,
			WCD9335_HPH_PA_CTL2_FORCE_IQCTRL_DISABLE);
		snd_soc_component_update_bits(comp, WCD9335_HPH_CNP_WG_CTL,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_MASK,
				WCD9335_HPH_CNP_WG_CTL_CURR_LDIV_RATIO_500);
	}
}

static void wcd9335_codec_hph_mode_config(struct snd_soc_component *component,
					  int event, int mode)
{
	switch (mode) {
	case CLS_H_LP:
		wcd9335_codec_hph_lp_config(component, event);
		break;
	case CLS_H_LOHIFI:
		wcd9335_codec_hph_lohifi_config(component, event);
		break;
	case CLS_H_HIFI:
		wcd9335_codec_hph_hifi_config(component, event);
		break;
	}
}

static int wcd9335_codec_hphl_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kc,
					int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);
	int hph_mode = wcd->hph_mode;
	u8 dem_inp;
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Read DEM INP Select */
		dem_inp = snd_soc_component_read32(comp,
				WCD9335_CDC_RX1_RX_PATH_SEC0) & 0x03;
		if (((hph_mode == CLS_H_HIFI) || (hph_mode == CLS_H_LOHIFI) ||
				(hph_mode == CLS_H_LP)) && (dem_inp != 0x01)) {
			dev_err(comp->dev, "Incorrect DEM Input\n");
			return -EINVAL;
		}
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHL,
					((hph_mode == CLS_H_LOHIFI) ?
					 CLS_H_HIFI : hph_mode));

		wcd9335_codec_hph_mode_config(comp, event, hph_mode);

		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 1000us required as per HW requirement */
		usleep_range(1000, 1100);

		if (!(wcd_clsh_ctrl_get_state(wcd->clsh_ctrl) &
				WCD_CLSH_STATE_HPHR))
			wcd9335_codec_hph_mode_config(comp, event, hph_mode);

		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_POST_PA,
				WCD_CLSH_STATE_HPHL,
				((hph_mode == CLS_H_LOHIFI) ?
				 CLS_H_HIFI : hph_mode));
		break;
	};

	return ret;
}

static int wcd9335_codec_lineout_dac_event(struct snd_soc_dapm_widget *w,
					   struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);

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

static int wcd9335_codec_ear_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_EAR, CLS_H_NORMAL);

		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_EAR, CLS_H_NORMAL);
		break;
	};

	return ret;
}

static void wcd9335_codec_hph_post_pa_config(struct wcd9335_codec *wcd,
					     int mode, int event)
{
	u8 scale_val = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		switch (mode) {
		case CLS_H_HIFI:
			scale_val = 0x3;
			break;
		case CLS_H_LOHIFI:
			scale_val = 0x1;
			break;
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		scale_val = 0x6;
		break;
	}

	if (scale_val)
		snd_soc_component_update_bits(wcd->component,
					WCD9335_HPH_PA_CTL1,
					WCD9335_HPH_PA_GM3_IB_SCALE_MASK,
					scale_val << 1);
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (wcd->comp_enabled[COMPANDER_1] ||
		    wcd->comp_enabled[COMPANDER_2]) {
			/* GAIN Source Selection */
			snd_soc_component_update_bits(wcd->component,
					WCD9335_HPH_L_EN,
					WCD9335_HPH_GAIN_SRC_SEL_MASK,
					WCD9335_HPH_GAIN_SRC_SEL_COMPANDER);
			snd_soc_component_update_bits(wcd->component,
					WCD9335_HPH_R_EN,
					WCD9335_HPH_GAIN_SRC_SEL_MASK,
					WCD9335_HPH_GAIN_SRC_SEL_COMPANDER);
			snd_soc_component_update_bits(wcd->component,
					WCD9335_HPH_AUTO_CHOP,
					WCD9335_HPH_AUTO_CHOP_MASK,
					WCD9335_HPH_AUTO_CHOP_FORCE_ENABLE);
		}
		snd_soc_component_update_bits(wcd->component,
						WCD9335_HPH_L_EN,
						WCD9335_HPH_PA_GAIN_MASK,
						wcd->hph_l_gain);
		snd_soc_component_update_bits(wcd->component,
						WCD9335_HPH_R_EN,
						WCD9335_HPH_PA_GAIN_MASK,
						wcd->hph_r_gain);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event))
		snd_soc_component_update_bits(wcd->component,
				WCD9335_HPH_AUTO_CHOP,
				WCD9335_HPH_AUTO_CHOP_MASK,
				WCD9335_HPH_AUTO_CHOP_ENABLE_BY_CMPDR_GAIN);
}

static int wcd9335_codec_hphr_dac_event(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kc,
				      int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);
	int hph_mode = wcd->hph_mode;
	u8 dem_inp;
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:

		/* Read DEM INP Select */
		dem_inp = snd_soc_component_read32(comp,
				WCD9335_CDC_RX2_RX_PATH_SEC0) &
				WCD9335_CDC_RX_PATH_DEM_INP_SEL_MASK;
		if (((hph_mode == CLS_H_HIFI) || (hph_mode == CLS_H_LOHIFI) ||
		     (hph_mode == CLS_H_LP)) && (dem_inp != 0x01)) {
			dev_err(comp->dev, "DEM Input not set correctly, hph_mode: %d\n",
				hph_mode);
			return -EINVAL;
		}

		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl,
			     WCD_CLSH_EVENT_PRE_DAC,
			     WCD_CLSH_STATE_HPHR,
			     ((hph_mode == CLS_H_LOHIFI) ?
			       CLS_H_HIFI : hph_mode));

		wcd9335_codec_hph_mode_config(comp, event, hph_mode);

		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 1000us required as per HW requirement */
		usleep_range(1000, 1100);

		if (!(wcd_clsh_ctrl_get_state(wcd->clsh_ctrl) &
					WCD_CLSH_STATE_HPHL))
			wcd9335_codec_hph_mode_config(comp, event, hph_mode);

		wcd_clsh_ctrl_set_state(wcd->clsh_ctrl, WCD_CLSH_EVENT_POST_PA,
			     WCD_CLSH_STATE_HPHR, ((hph_mode == CLS_H_LOHIFI) ?
						CLS_H_HIFI : hph_mode));
		break;
	};

	return ret;
}

static int wcd9335_codec_enable_hphl_pa(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kc,
				      int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);
	int hph_mode = wcd->hph_mode;
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required after PA is enabled as per
		 * HW requirement
		 */
		usleep_range(7000, 7100);

		wcd9335_codec_hph_post_pa_config(wcd, hph_mode, event);
		snd_soc_component_update_bits(comp,
					WCD9335_CDC_RX1_RX_PATH_CTL,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_DISABLE);

		/* Remove mix path mute if it is enabled */
		if ((snd_soc_component_read32(comp,
					WCD9335_CDC_RX1_RX_PATH_MIX_CTL)) &
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK)
			snd_soc_component_update_bits(comp,
					    WCD9335_CDC_RX1_RX_PATH_MIX_CTL,
					    WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					    WCD9335_CDC_RX_PGA_MUTE_DISABLE);

		break;
	case SND_SOC_DAPM_PRE_PMD:
		wcd9335_codec_hph_post_pa_config(wcd, hph_mode, event);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 5ms sleep is required after PA is disabled as per
		 * HW requirement
		 */
		usleep_range(5000, 5500);
		break;
	};

	return ret;
}

static int wcd9335_codec_enable_lineout_pa(struct snd_soc_dapm_widget *w,
					 struct snd_kcontrol *kc,
					 int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	int vol_reg = 0, mix_vol_reg = 0;
	int ret = 0;

	if (w->reg == WCD9335_ANA_LO_1_2) {
		if (w->shift == 7) {
			vol_reg = WCD9335_CDC_RX3_RX_PATH_CTL;
			mix_vol_reg = WCD9335_CDC_RX3_RX_PATH_MIX_CTL;
		} else if (w->shift == 6) {
			vol_reg = WCD9335_CDC_RX4_RX_PATH_CTL;
			mix_vol_reg = WCD9335_CDC_RX4_RX_PATH_MIX_CTL;
		}
	} else if (w->reg == WCD9335_ANA_LO_3_4) {
		if (w->shift == 7) {
			vol_reg = WCD9335_CDC_RX5_RX_PATH_CTL;
			mix_vol_reg = WCD9335_CDC_RX5_RX_PATH_MIX_CTL;
		} else if (w->shift == 6) {
			vol_reg = WCD9335_CDC_RX6_RX_PATH_CTL;
			mix_vol_reg = WCD9335_CDC_RX6_RX_PATH_MIX_CTL;
		}
	} else {
		dev_err(comp->dev, "Error enabling lineout PA\n");
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* 5ms sleep is required after PA is enabled as per
		 * HW requirement
		 */
		usleep_range(5000, 5500);
		snd_soc_component_update_bits(comp, vol_reg,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_DISABLE);

		/* Remove mix path mute if it is enabled */
		if ((snd_soc_component_read32(comp, mix_vol_reg)) &
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK)
			snd_soc_component_update_bits(comp,  mix_vol_reg,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_DISABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 5ms sleep is required after PA is disabled as per
		 * HW requirement
		 */
		usleep_range(5000, 5500);
		break;
	};

	return ret;
}

static void wcd9335_codec_init_flyback(struct snd_soc_component *component)
{
	snd_soc_component_update_bits(component, WCD9335_HPH_L_EN,
					WCD9335_HPH_CONST_SEL_L_MASK,
					WCD9335_HPH_CONST_SEL_L_BYPASS);
	snd_soc_component_update_bits(component, WCD9335_HPH_R_EN,
					WCD9335_HPH_CONST_SEL_L_MASK,
					WCD9335_HPH_CONST_SEL_L_BYPASS);
	snd_soc_component_update_bits(component, WCD9335_RX_BIAS_FLYB_BUFF,
					WCD9335_RX_BIAS_FLYB_VPOS_5_UA_MASK,
					WCD9335_RX_BIAS_FLYB_I_0P0_UA);
	snd_soc_component_update_bits(component, WCD9335_RX_BIAS_FLYB_BUFF,
					WCD9335_RX_BIAS_FLYB_VNEG_5_UA_MASK,
					WCD9335_RX_BIAS_FLYB_I_0P0_UA);
}

static int wcd9335_codec_enable_rx_bias(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd->rx_bias_count++;
		if (wcd->rx_bias_count == 1) {
			wcd9335_codec_init_flyback(comp);
			snd_soc_component_update_bits(comp,
						WCD9335_ANA_RX_SUPPLIES,
						WCD9335_ANA_RX_BIAS_ENABLE_MASK,
						WCD9335_ANA_RX_BIAS_ENABLE);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd->rx_bias_count--;
		if (!wcd->rx_bias_count)
			snd_soc_component_update_bits(comp,
					WCD9335_ANA_RX_SUPPLIES,
					WCD9335_ANA_RX_BIAS_ENABLE_MASK,
					WCD9335_ANA_RX_BIAS_DISABLE);
		break;
	};

	return 0;
}

static int wcd9335_codec_enable_hphr_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);
	int hph_mode = wcd->hph_mode;
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required after PA is enabled as per
		 * HW requirement
		 */
		usleep_range(7000, 7100);
		wcd9335_codec_hph_post_pa_config(wcd, hph_mode, event);
		snd_soc_component_update_bits(comp,
					WCD9335_CDC_RX2_RX_PATH_CTL,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_DISABLE);
		/* Remove mix path mute if it is enabled */
		if ((snd_soc_component_read32(comp,
					WCD9335_CDC_RX2_RX_PATH_MIX_CTL)) &
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK)
			snd_soc_component_update_bits(comp,
					WCD9335_CDC_RX2_RX_PATH_MIX_CTL,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_DISABLE);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		wcd9335_codec_hph_post_pa_config(wcd, hph_mode, event);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 5ms sleep is required after PA is disabled as per
		 * HW requirement
		 */
		usleep_range(5000, 5500);
		break;
	};

	return ret;
}

static int wcd9335_codec_enable_ear_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* 5ms sleep is required after PA is enabled as per
		 * HW requirement
		 */
		usleep_range(5000, 5500);
		snd_soc_component_update_bits(comp,
					WCD9335_CDC_RX0_RX_PATH_CTL,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_DISABLE);
		/* Remove mix path mute if it is enabled */
		if ((snd_soc_component_read32(comp,
					WCD9335_CDC_RX0_RX_PATH_MIX_CTL)) &
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK)
			snd_soc_component_update_bits(comp,
					WCD9335_CDC_RX0_RX_PATH_MIX_CTL,
					WCD9335_CDC_RX_PGA_MUTE_EN_MASK,
					WCD9335_CDC_RX_PGA_MUTE_DISABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 5ms sleep is required after PA is disabled as per
		 * HW requirement
		 */
		usleep_range(5000, 5500);

		break;
	};

	return ret;
}

static irqreturn_t wcd9335_slimbus_irq(int irq, void *data)
{
	struct wcd9335_codec *wcd = data;
	unsigned long status = 0;
	int i, j, port_id;
	unsigned int val, int_val = 0;
	irqreturn_t ret = IRQ_NONE;
	bool tx;
	unsigned short reg = 0;

	for (i = WCD9335_SLIM_PGD_PORT_INT_STATUS_RX_0, j = 0;
	     i <= WCD9335_SLIM_PGD_PORT_INT_STATUS_TX_1; i++, j++) {
		regmap_read(wcd->if_regmap, i, &val);
		status |= ((u32)val << (8 * j));
	}

	for_each_set_bit(j, &status, 32) {
		tx = (j >= 16 ? true : false);
		port_id = (tx ? j - 16 : j);
		regmap_read(wcd->if_regmap,
				WCD9335_SLIM_PGD_PORT_INT_RX_SOURCE0 + j, &val);
		if (val) {
			if (!tx)
				reg = WCD9335_SLIM_PGD_PORT_INT_EN0 +
					(port_id / 8);
			else
				reg = WCD9335_SLIM_PGD_PORT_INT_TX_EN0 +
					(port_id / 8);
			regmap_read(
				wcd->if_regmap, reg, &int_val);
			/*
			 * Ignore interrupts for ports for which the
			 * interrupts are not specifically enabled.
			 */
			if (!(int_val & (1 << (port_id % 8))))
				continue;
		}

		if (val & WCD9335_SLIM_IRQ_OVERFLOW)
			dev_err_ratelimited(wcd->dev,
			   "%s: overflow error on %s port %d, value %x\n",
			   __func__, (tx ? "TX" : "RX"), port_id, val);

		if (val & WCD9335_SLIM_IRQ_UNDERFLOW)
			dev_err_ratelimited(wcd->dev,
			   "%s: underflow error on %s port %d, value %x\n",
			   __func__, (tx ? "TX" : "RX"), port_id, val);

		if ((val & WCD9335_SLIM_IRQ_OVERFLOW) ||
			(val & WCD9335_SLIM_IRQ_UNDERFLOW)) {
			if (!tx)
				reg = WCD9335_SLIM_PGD_PORT_INT_EN0 +
					(port_id / 8);
			else
				reg = WCD9335_SLIM_PGD_PORT_INT_TX_EN0 +
					(port_id / 8);
			regmap_read(
				wcd->if_regmap, reg, &int_val);
			if (int_val & (1 << (port_id % 8))) {
				int_val = int_val ^ (1 << (port_id % 8));
				regmap_write(wcd->if_regmap,
					reg, int_val);
			}
		}

		regmap_write(wcd->if_regmap,
				WCD9335_SLIM_PGD_PORT_INT_CLR_RX_0 + (j / 8),
				BIT(j % 8));
		ret = IRQ_HANDLED;
	}

	return ret;
}

static struct wcd9335_irq wcd9335_irqs[] = {
	{
		.irq = WCD9335_IRQ_SLIMBUS,
		.handler = wcd9335_slimbus_irq,
		.name = "SLIM Slave",
	},
};

static int wcd9335_setup_irqs(struct wcd9335_codec *wcd)
{
	int irq, ret, i;

	for (i = 0; i < ARRAY_SIZE(wcd9335_irqs); i++) {
		irq = regmap_irq_get_virq(wcd->irq_data, wcd9335_irqs[i].irq);
		if (irq < 0) {
			dev_err(wcd->dev, "Failed to get %s\n",
					wcd9335_irqs[i].name);
			return irq;
		}

		ret = devm_request_threaded_irq(wcd->dev, irq, NULL,
						wcd9335_irqs[i].handler,
						IRQF_TRIGGER_RISING,
						wcd9335_irqs[i].name, wcd);
		if (ret) {
			dev_err(wcd->dev, "Failed to request %s\n",
					wcd9335_irqs[i].name);
			return ret;
		}
	}

	/* enable interrupts on all slave ports */
	for (i = 0; i < WCD9335_SLIM_NUM_PORT_REG; i++)
		regmap_write(wcd->if_regmap, WCD9335_SLIM_PGD_PORT_INT_EN0 + i,
			     0xFF);

	return ret;
}

static void wcd9335_cdc_sido_ccl_enable(struct wcd9335_codec *wcd,
					bool ccl_flag)
{
	struct snd_soc_component *comp = wcd->component;

	if (ccl_flag) {
		if (++wcd->sido_ccl_cnt == 1)
			snd_soc_component_write(comp, WCD9335_SIDO_SIDO_CCL_10,
					WCD9335_SIDO_SIDO_CCL_DEF_VALUE);
	} else {
		if (wcd->sido_ccl_cnt == 0) {
			dev_err(wcd->dev, "sido_ccl already disabled\n");
			return;
		}
		if (--wcd->sido_ccl_cnt == 0)
			snd_soc_component_write(comp, WCD9335_SIDO_SIDO_CCL_10,
				WCD9335_SIDO_SIDO_CCL_10_ICHARG_PWR_SEL_C320FF);
	}
}

static int wcd9335_enable_master_bias(struct wcd9335_codec *wcd)
{
	wcd->master_bias_users++;
	if (wcd->master_bias_users == 1) {
		regmap_update_bits(wcd->regmap, WCD9335_ANA_BIAS,
					WCD9335_ANA_BIAS_EN_MASK,
					WCD9335_ANA_BIAS_ENABLE);
		regmap_update_bits(wcd->regmap, WCD9335_ANA_BIAS,
					WCD9335_ANA_BIAS_PRECHRG_EN_MASK,
					WCD9335_ANA_BIAS_PRECHRG_ENABLE);
		/*
		 * 1ms delay is required after pre-charge is enabled
		 * as per HW requirement
		 */
		usleep_range(1000, 1100);
		regmap_update_bits(wcd->regmap, WCD9335_ANA_BIAS,
					WCD9335_ANA_BIAS_PRECHRG_EN_MASK,
					WCD9335_ANA_BIAS_PRECHRG_DISABLE);
		regmap_update_bits(wcd->regmap, WCD9335_ANA_BIAS,
				WCD9335_ANA_BIAS_PRECHRG_CTL_MODE,
				WCD9335_ANA_BIAS_PRECHRG_CTL_MODE_MANUAL);
	}

	return 0;
}

static int wcd9335_enable_mclk(struct wcd9335_codec *wcd)
{
	/* Enable mclk requires master bias to be enabled first */
	if (wcd->master_bias_users <= 0)
		return -EINVAL;

	if (((wcd->clk_mclk_users == 0) && (wcd->clk_type == WCD_CLK_MCLK)) ||
	    ((wcd->clk_mclk_users > 0) && (wcd->clk_type != WCD_CLK_MCLK))) {
		dev_err(wcd->dev, "Error enabling MCLK, clk_type: %d\n",
			wcd->clk_type);
		return -EINVAL;
	}

	if (++wcd->clk_mclk_users == 1) {
		regmap_update_bits(wcd->regmap, WCD9335_ANA_CLK_TOP,
					WCD9335_ANA_CLK_EXT_CLKBUF_EN_MASK,
					WCD9335_ANA_CLK_EXT_CLKBUF_ENABLE);
		regmap_update_bits(wcd->regmap, WCD9335_ANA_CLK_TOP,
					WCD9335_ANA_CLK_MCLK_SRC_MASK,
					WCD9335_ANA_CLK_MCLK_SRC_EXTERNAL);
		regmap_update_bits(wcd->regmap, WCD9335_ANA_CLK_TOP,
					WCD9335_ANA_CLK_MCLK_EN_MASK,
					WCD9335_ANA_CLK_MCLK_ENABLE);
		regmap_update_bits(wcd->regmap,
				   WCD9335_CDC_CLK_RST_CTRL_FS_CNT_CONTROL,
				   WCD9335_CDC_CLK_RST_CTRL_FS_CNT_EN_MASK,
				   WCD9335_CDC_CLK_RST_CTRL_FS_CNT_ENABLE);
		regmap_update_bits(wcd->regmap,
				   WCD9335_CDC_CLK_RST_CTRL_MCLK_CONTROL,
				   WCD9335_CDC_CLK_RST_CTRL_MCLK_EN_MASK,
				   WCD9335_CDC_CLK_RST_CTRL_MCLK_ENABLE);
		/*
		 * 10us sleep is required after clock is enabled
		 * as per HW requirement
		 */
		usleep_range(10, 15);
	}

	wcd->clk_type = WCD_CLK_MCLK;

	return 0;
}

static int wcd9335_disable_mclk(struct wcd9335_codec *wcd)
{
	if (wcd->clk_mclk_users <= 0)
		return -EINVAL;

	if (--wcd->clk_mclk_users == 0) {
		if (wcd->clk_rco_users > 0) {
			/* MCLK to RCO switch */
			regmap_update_bits(wcd->regmap, WCD9335_ANA_CLK_TOP,
					WCD9335_ANA_CLK_MCLK_SRC_MASK,
					WCD9335_ANA_CLK_MCLK_SRC_RCO);
			wcd->clk_type = WCD_CLK_RCO;
		} else {
			regmap_update_bits(wcd->regmap, WCD9335_ANA_CLK_TOP,
					WCD9335_ANA_CLK_MCLK_EN_MASK,
					WCD9335_ANA_CLK_MCLK_DISABLE);
			wcd->clk_type = WCD_CLK_OFF;
		}

		regmap_update_bits(wcd->regmap, WCD9335_ANA_CLK_TOP,
					WCD9335_ANA_CLK_EXT_CLKBUF_EN_MASK,
					WCD9335_ANA_CLK_EXT_CLKBUF_DISABLE);
	}

	return 0;
}

static int wcd9335_disable_master_bias(struct wcd9335_codec *wcd)
{
	if (wcd->master_bias_users <= 0)
		return -EINVAL;

	wcd->master_bias_users--;
	if (wcd->master_bias_users == 0) {
		regmap_update_bits(wcd->regmap, WCD9335_ANA_BIAS,
				WCD9335_ANA_BIAS_EN_MASK,
				WCD9335_ANA_BIAS_DISABLE);
		regmap_update_bits(wcd->regmap, WCD9335_ANA_BIAS,
				WCD9335_ANA_BIAS_PRECHRG_CTL_MODE,
				WCD9335_ANA_BIAS_PRECHRG_CTL_MODE_MANUAL);
	}
	return 0;
}

static int wcd9335_cdc_req_mclk_enable(struct wcd9335_codec *wcd,
				     bool enable)
{
	int ret = 0;

	if (enable) {
		wcd9335_cdc_sido_ccl_enable(wcd, true);
		ret = clk_prepare_enable(wcd->mclk);
		if (ret) {
			dev_err(wcd->dev, "%s: ext clk enable failed\n",
				__func__);
			goto err;
		}
		/* get BG */
		wcd9335_enable_master_bias(wcd);
		/* get MCLK */
		wcd9335_enable_mclk(wcd);

	} else {
		/* put MCLK */
		wcd9335_disable_mclk(wcd);
		/* put BG */
		wcd9335_disable_master_bias(wcd);
		clk_disable_unprepare(wcd->mclk);
		wcd9335_cdc_sido_ccl_enable(wcd, false);
	}
err:
	return ret;
}

static void wcd9335_codec_apply_sido_voltage(struct wcd9335_codec *wcd,
					     enum wcd9335_sido_voltage req_mv)
{
	struct snd_soc_component *comp = wcd->component;
	int vout_d_val;

	if (req_mv == wcd->sido_voltage)
		return;

	/* compute the vout_d step value */
	vout_d_val = WCD9335_CALCULATE_VOUT_D(req_mv) &
			WCD9335_ANA_BUCK_VOUT_MASK;
	snd_soc_component_write(comp, WCD9335_ANA_BUCK_VOUT_D, vout_d_val);
	snd_soc_component_update_bits(comp, WCD9335_ANA_BUCK_CTL,
				WCD9335_ANA_BUCK_CTL_RAMP_START_MASK,
				WCD9335_ANA_BUCK_CTL_RAMP_START_ENABLE);

	/* 1 msec sleep required after SIDO Vout_D voltage change */
	usleep_range(1000, 1100);
	wcd->sido_voltage = req_mv;
	snd_soc_component_update_bits(comp, WCD9335_ANA_BUCK_CTL,
				WCD9335_ANA_BUCK_CTL_RAMP_START_MASK,
				WCD9335_ANA_BUCK_CTL_RAMP_START_DISABLE);
}

static int wcd9335_codec_update_sido_voltage(struct wcd9335_codec *wcd,
					     enum wcd9335_sido_voltage req_mv)
{
	int ret = 0;

	/* enable mclk before setting SIDO voltage */
	ret = wcd9335_cdc_req_mclk_enable(wcd, true);
	if (ret) {
		dev_err(wcd->dev, "Ext clk enable failed\n");
		goto err;
	}

	wcd9335_codec_apply_sido_voltage(wcd, req_mv);
	wcd9335_cdc_req_mclk_enable(wcd, false);

err:
	return ret;
}

static int _wcd9335_codec_enable_mclk(struct snd_soc_component *component,
				      int enable)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	int ret;

	if (enable) {
		ret = wcd9335_cdc_req_mclk_enable(wcd, true);
		if (ret)
			return ret;

		wcd9335_codec_apply_sido_voltage(wcd,
				SIDO_VOLTAGE_NOMINAL_MV);
	} else {
		wcd9335_codec_update_sido_voltage(wcd,
					wcd->sido_voltage);
		wcd9335_cdc_req_mclk_enable(wcd, false);
	}

	return 0;
}

static int wcd9335_codec_enable_mclk(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kc, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return _wcd9335_codec_enable_mclk(comp, true);
	case SND_SOC_DAPM_POST_PMD:
		return _wcd9335_codec_enable_mclk(comp, false);
	}

	return 0;
}

static const struct snd_soc_dapm_widget wcd9335_dapm_widgets[] = {
	/* TODO SPK1 & SPK2 OUT*/
	SND_SOC_DAPM_OUTPUT("EAR"),
	SND_SOC_DAPM_OUTPUT("HPHL"),
	SND_SOC_DAPM_OUTPUT("HPHR"),
	SND_SOC_DAPM_OUTPUT("LINEOUT1"),
	SND_SOC_DAPM_OUTPUT("LINEOUT2"),
	SND_SOC_DAPM_OUTPUT("LINEOUT3"),
	SND_SOC_DAPM_OUTPUT("LINEOUT4"),
	SND_SOC_DAPM_AIF_IN_E("AIF1 PB", "AIF1 Playback", 0, SND_SOC_NOPM,
				AIF1_PB, 0, wcd9335_codec_enable_slim,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AIF2 PB", "AIF2 Playback", 0, SND_SOC_NOPM,
				AIF2_PB, 0, wcd9335_codec_enable_slim,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AIF3 PB", "AIF3 Playback", 0, SND_SOC_NOPM,
				AIF3_PB, 0, wcd9335_codec_enable_slim,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AIF4 PB", "AIF4 Playback", 0, SND_SOC_NOPM,
				AIF4_PB, 0, wcd9335_codec_enable_slim,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX("SLIM RX0 MUX", SND_SOC_NOPM, WCD9335_RX0, 0,
				&slim_rx_mux[WCD9335_RX0]),
	SND_SOC_DAPM_MUX("SLIM RX1 MUX", SND_SOC_NOPM, WCD9335_RX1, 0,
				&slim_rx_mux[WCD9335_RX1]),
	SND_SOC_DAPM_MUX("SLIM RX2 MUX", SND_SOC_NOPM, WCD9335_RX2, 0,
				&slim_rx_mux[WCD9335_RX2]),
	SND_SOC_DAPM_MUX("SLIM RX3 MUX", SND_SOC_NOPM, WCD9335_RX3, 0,
				&slim_rx_mux[WCD9335_RX3]),
	SND_SOC_DAPM_MUX("SLIM RX4 MUX", SND_SOC_NOPM, WCD9335_RX4, 0,
				&slim_rx_mux[WCD9335_RX4]),
	SND_SOC_DAPM_MUX("SLIM RX5 MUX", SND_SOC_NOPM, WCD9335_RX5, 0,
				&slim_rx_mux[WCD9335_RX5]),
	SND_SOC_DAPM_MUX("SLIM RX6 MUX", SND_SOC_NOPM, WCD9335_RX6, 0,
				&slim_rx_mux[WCD9335_RX6]),
	SND_SOC_DAPM_MUX("SLIM RX7 MUX", SND_SOC_NOPM, WCD9335_RX7, 0,
				&slim_rx_mux[WCD9335_RX7]),
	SND_SOC_DAPM_MIXER("SLIM RX0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX6", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SLIM RX7", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX_E("RX INT0_2 MUX", WCD9335_CDC_RX0_RX_PATH_MIX_CTL,
			5, 0, &rx_int0_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT1_2 MUX", WCD9335_CDC_RX1_RX_PATH_MIX_CTL,
			5, 0, &rx_int1_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT2_2 MUX", WCD9335_CDC_RX2_RX_PATH_MIX_CTL,
			5, 0, &rx_int2_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT3_2 MUX", WCD9335_CDC_RX3_RX_PATH_MIX_CTL,
			5, 0, &rx_int3_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT4_2 MUX", WCD9335_CDC_RX4_RX_PATH_MIX_CTL,
			5, 0, &rx_int4_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT5_2 MUX", WCD9335_CDC_RX5_RX_PATH_MIX_CTL,
			5, 0, &rx_int5_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT6_2 MUX", WCD9335_CDC_RX6_RX_PATH_MIX_CTL,
			5, 0, &rx_int6_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT7_2 MUX", WCD9335_CDC_RX7_RX_PATH_MIX_CTL,
			5, 0, &rx_int7_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("RX INT8_2 MUX", WCD9335_CDC_RX8_RX_PATH_MIX_CTL,
			5, 0, &rx_int8_2_mux, wcd9335_codec_enable_mix_path,
			SND_SOC_DAPM_POST_PMU),
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
	SND_SOC_DAPM_MUX("RX INT5_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
		&rx_int5_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT5_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
		&rx_int5_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT5_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
		&rx_int5_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT6_1 MIX1 INP0", SND_SOC_NOPM, 0, 0,
		&rx_int6_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT6_1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
		&rx_int6_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT6_1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
		&rx_int6_1_mix_inp2_mux),
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
	SND_SOC_DAPM_MIXER("RX INT1 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT3_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT3 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT4_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT4 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT5_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT5 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT6_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT6 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT7_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT7 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT8_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT8 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("RX INT0 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT3 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT4 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT5 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT6 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT7 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT8 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("RX INT0 DEM MUX", SND_SOC_NOPM, 0, 0,
		&rx_int0_dem_inp_mux),
	SND_SOC_DAPM_MUX("RX INT1 DEM MUX", SND_SOC_NOPM, 0, 0,
		&rx_int1_dem_inp_mux),
	SND_SOC_DAPM_MUX("RX INT2 DEM MUX", SND_SOC_NOPM, 0, 0,
		&rx_int2_dem_inp_mux),

	SND_SOC_DAPM_MUX_E("RX INT0 INTERP", SND_SOC_NOPM,
		INTERP_EAR, 0, &rx_int0_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT1 INTERP", SND_SOC_NOPM,
		INTERP_HPHL, 0, &rx_int1_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT2 INTERP", SND_SOC_NOPM,
		INTERP_HPHR, 0, &rx_int2_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT3 INTERP", SND_SOC_NOPM,
		INTERP_LO1, 0, &rx_int3_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT4 INTERP", SND_SOC_NOPM,
		INTERP_LO2, 0, &rx_int4_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT5 INTERP", SND_SOC_NOPM,
		INTERP_LO3, 0, &rx_int5_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT6 INTERP", SND_SOC_NOPM,
		INTERP_LO4, 0, &rx_int6_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT7 INTERP", SND_SOC_NOPM,
		INTERP_SPKR1, 0, &rx_int7_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT8 INTERP", SND_SOC_NOPM,
		INTERP_SPKR2, 0, &rx_int8_interp_mux,
		wcd9335_codec_enable_interpolator,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_DAC_E("RX INT0 DAC", NULL, SND_SOC_NOPM,
		0, 0, wcd9335_codec_ear_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT1 DAC", NULL, WCD9335_ANA_HPH,
		5, 0, wcd9335_codec_hphl_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT2 DAC", NULL, WCD9335_ANA_HPH,
		4, 0, wcd9335_codec_hphr_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT3 DAC", NULL, SND_SOC_NOPM,
		0, 0, wcd9335_codec_lineout_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT4 DAC", NULL, SND_SOC_NOPM,
		0, 0, wcd9335_codec_lineout_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT5 DAC", NULL, SND_SOC_NOPM,
		0, 0, wcd9335_codec_lineout_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RX INT6 DAC", NULL, SND_SOC_NOPM,
		0, 0, wcd9335_codec_lineout_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHL PA", WCD9335_ANA_HPH, 7, 0, NULL, 0,
			   wcd9335_codec_enable_hphl_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHR PA", WCD9335_ANA_HPH, 6, 0, NULL, 0,
			   wcd9335_codec_enable_hphr_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("EAR PA", WCD9335_ANA_EAR, 7, 0, NULL, 0,
			   wcd9335_codec_enable_ear_pa,
			   SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LINEOUT1 PA", WCD9335_ANA_LO_1_2, 7, 0, NULL, 0,
			   wcd9335_codec_enable_lineout_pa,
			   SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LINEOUT2 PA", WCD9335_ANA_LO_1_2, 6, 0, NULL, 0,
			   wcd9335_codec_enable_lineout_pa,
			   SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LINEOUT3 PA", WCD9335_ANA_LO_3_4, 7, 0, NULL, 0,
			   wcd9335_codec_enable_lineout_pa,
			   SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LINEOUT4 PA", WCD9335_ANA_LO_3_4, 6, 0, NULL, 0,
			   wcd9335_codec_enable_lineout_pa,
			   SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("RX_BIAS", SND_SOC_NOPM, 0, 0,
		wcd9335_codec_enable_rx_bias, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MCLK",  SND_SOC_NOPM, 0, 0,
		wcd9335_codec_enable_mclk, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

};

static void wcd9335_enable_sido_buck(struct snd_soc_component *component)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);

	snd_soc_component_update_bits(component, WCD9335_ANA_RCO,
					WCD9335_ANA_RCO_BG_EN_MASK,
					WCD9335_ANA_RCO_BG_ENABLE);
	snd_soc_component_update_bits(component, WCD9335_ANA_BUCK_CTL,
					WCD9335_ANA_BUCK_CTL_VOUT_D_IREF_MASK,
					WCD9335_ANA_BUCK_CTL_VOUT_D_IREF_EXT);
	/* 100us sleep needed after IREF settings */
	usleep_range(100, 110);
	snd_soc_component_update_bits(component, WCD9335_ANA_BUCK_CTL,
					WCD9335_ANA_BUCK_CTL_VOUT_D_VREF_MASK,
					WCD9335_ANA_BUCK_CTL_VOUT_D_VREF_EXT);
	/* 100us sleep needed after VREF settings */
	usleep_range(100, 110);
	wcd->sido_input_src = SIDO_SOURCE_RCO_BG;
}

static int wcd9335_enable_efuse_sensing(struct snd_soc_component *comp)
{
	_wcd9335_codec_enable_mclk(comp, true);
	snd_soc_component_update_bits(comp,
				WCD9335_CHIP_TIER_CTRL_EFUSE_CTL,
				WCD9335_CHIP_TIER_CTRL_EFUSE_EN_MASK,
				WCD9335_CHIP_TIER_CTRL_EFUSE_ENABLE);
	/*
	 * 5ms sleep required after enabling efuse control
	 * before checking the status.
	 */
	usleep_range(5000, 5500);

	if (!(snd_soc_component_read32(comp,
					WCD9335_CHIP_TIER_CTRL_EFUSE_STATUS) &
					WCD9335_CHIP_TIER_CTRL_EFUSE_EN_MASK))
		WARN(1, "%s: Efuse sense is not complete\n", __func__);

	wcd9335_enable_sido_buck(comp);
	_wcd9335_codec_enable_mclk(comp, false);

	return 0;
}

static void wcd9335_codec_init(struct snd_soc_component *component)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	int i;

	/* ungate MCLK and set clk rate */
	regmap_update_bits(wcd->regmap, WCD9335_CODEC_RPM_CLK_GATE,
				WCD9335_CODEC_RPM_CLK_GATE_MCLK_GATE_MASK, 0);

	regmap_update_bits(wcd->regmap, WCD9335_CODEC_RPM_CLK_MCLK_CFG,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG_MCLK_MASK,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG_9P6MHZ);

	for (i = 0; i < ARRAY_SIZE(wcd9335_codec_reg_init); i++)
		snd_soc_component_update_bits(component,
					wcd9335_codec_reg_init[i].reg,
					wcd9335_codec_reg_init[i].mask,
					wcd9335_codec_reg_init[i].val);

	wcd9335_enable_efuse_sensing(component);
}

static int wcd9335_codec_probe(struct snd_soc_component *component)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(component->dev);
	int i;

	snd_soc_component_init_regmap(component, wcd->regmap);
	/* Class-H Init*/
	wcd->clsh_ctrl = wcd_clsh_ctrl_alloc(component, wcd->version);
	if (IS_ERR(wcd->clsh_ctrl))
		return PTR_ERR(wcd->clsh_ctrl);

	/* Default HPH Mode to Class-H HiFi */
	wcd->hph_mode = CLS_H_HIFI;
	wcd->component = component;

	wcd9335_codec_init(component);

	for (i = 0; i < NUM_CODEC_DAIS; i++)
		INIT_LIST_HEAD(&wcd->dai[i].slim_ch_list);

	return wcd9335_setup_irqs(wcd);
}

static void wcd9335_codec_remove(struct snd_soc_component *comp)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);

	wcd_clsh_ctrl_free(wcd->clsh_ctrl);
	free_irq(regmap_irq_get_virq(wcd->irq_data, WCD9335_IRQ_SLIMBUS), wcd);
}

static int wcd9335_codec_set_sysclk(struct snd_soc_component *comp,
				    int clk_id, int source,
				    unsigned int freq, int dir)
{
	struct wcd9335_codec *wcd = dev_get_drvdata(comp->dev);

	wcd->mclk_rate = freq;

	if (wcd->mclk_rate == WCD9335_MCLK_CLK_12P288MHZ)
		snd_soc_component_update_bits(comp,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG_MCLK_MASK,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG_12P288MHZ);
	else if (wcd->mclk_rate == WCD9335_MCLK_CLK_9P6MHZ)
		snd_soc_component_update_bits(comp,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG_MCLK_MASK,
				WCD9335_CODEC_RPM_CLK_MCLK_CFG_9P6MHZ);

	return clk_set_rate(wcd->mclk, freq);
}

static const struct snd_soc_component_driver wcd9335_component_drv = {
	.probe = wcd9335_codec_probe,
	.remove = wcd9335_codec_remove,
	.set_sysclk = wcd9335_codec_set_sysclk,
	.controls = wcd9335_snd_controls,
	.num_controls = ARRAY_SIZE(wcd9335_snd_controls),
	.dapm_widgets = wcd9335_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wcd9335_dapm_widgets),
};

static int wcd9335_probe(struct wcd9335_codec *wcd)
{
	struct device *dev = wcd->dev;

	memcpy(wcd->rx_chs, wcd9335_rx_chs, sizeof(wcd9335_rx_chs));

	wcd->sido_input_src = SIDO_SOURCE_INTERNAL;
	wcd->sido_voltage = SIDO_VOLTAGE_NOMINAL_MV;

	return devm_snd_soc_register_component(dev, &wcd9335_component_drv,
					       wcd9335_slim_dais,
					       ARRAY_SIZE(wcd9335_slim_dais));
}

static const struct regmap_range_cfg wcd9335_ranges[] = {
	{
		.name = "WCD9335",
		.range_min =  0x0,
		.range_max =  WCD9335_MAX_REGISTER,
		.selector_reg = WCD9335_REG(0x0, 0),
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0x0,
		.window_len = 0x1000,
	},
};

static bool wcd9335_is_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WCD9335_INTR_PIN1_STATUS0...WCD9335_INTR_PIN2_CLEAR3:
	case WCD9335_ANA_MBHC_RESULT_3:
	case WCD9335_ANA_MBHC_RESULT_2:
	case WCD9335_ANA_MBHC_RESULT_1:
	case WCD9335_ANA_MBHC_MECH:
	case WCD9335_ANA_MBHC_ELECT:
	case WCD9335_ANA_MBHC_ZDET:
	case WCD9335_ANA_MICB2:
	case WCD9335_ANA_RCO:
	case WCD9335_ANA_BIAS:
		return true;
	default:
		return false;
	}
}

static struct regmap_config wcd9335_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = WCD9335_MAX_REGISTER,
	.can_multi_write = true,
	.ranges = wcd9335_ranges,
	.num_ranges = ARRAY_SIZE(wcd9335_ranges),
	.volatile_reg = wcd9335_is_volatile_register,
};

static const struct regmap_range_cfg wcd9335_ifc_ranges[] = {
	{
		.name = "WCD9335-IFC-DEV",
		.range_min =  0x0,
		.range_max = WCD9335_REG(0, 0x7ff),
		.selector_reg = WCD9335_REG(0, 0x0),
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0x0,
		.window_len = 0x1000,
	},
};

static struct regmap_config wcd9335_ifc_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.can_multi_write = true,
	.max_register = WCD9335_REG(0, 0x7FF),
	.ranges = wcd9335_ifc_ranges,
	.num_ranges = ARRAY_SIZE(wcd9335_ifc_ranges),
};

static const struct regmap_irq wcd9335_codec_irqs[] = {
	/* INTR_REG 0 */
	[WCD9335_IRQ_SLIMBUS] = {
		.reg_offset = 0,
		.mask = BIT(0),
		.type = {
			.type_reg_offset = 0,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
			.type_reg_mask	= BIT(0),
		},
	},
};

static const struct regmap_irq_chip wcd9335_regmap_irq1_chip = {
	.name = "wcd9335_pin1_irq",
	.status_base = WCD9335_INTR_PIN1_STATUS0,
	.mask_base = WCD9335_INTR_PIN1_MASK0,
	.ack_base = WCD9335_INTR_PIN1_CLEAR0,
	.type_base = WCD9335_INTR_LEVEL0,
	.num_type_reg = 4,
	.num_regs = 4,
	.irqs = wcd9335_codec_irqs,
	.num_irqs = ARRAY_SIZE(wcd9335_codec_irqs),
};

static int wcd9335_parse_dt(struct wcd9335_codec *wcd)
{
	struct device *dev = wcd->dev;
	struct device_node *np = dev->of_node;
	int ret;

	wcd->reset_gpio = of_get_named_gpio(np,	"reset-gpios", 0);
	if (wcd->reset_gpio < 0) {
		dev_err(dev, "Reset GPIO missing from DT\n");
		return wcd->reset_gpio;
	}

	wcd->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(wcd->mclk)) {
		dev_err(dev, "mclk not found\n");
		return PTR_ERR(wcd->mclk);
	}

	wcd->native_clk = devm_clk_get(dev, "slimbus");
	if (IS_ERR(wcd->native_clk)) {
		dev_err(dev, "slimbus clock not found\n");
		return PTR_ERR(wcd->native_clk);
	}

	wcd->supplies[0].supply = "vdd-buck";
	wcd->supplies[1].supply = "vdd-buck-sido";
	wcd->supplies[2].supply = "vdd-tx";
	wcd->supplies[3].supply = "vdd-rx";
	wcd->supplies[4].supply = "vdd-io";

	ret = regulator_bulk_get(dev, WCD9335_MAX_SUPPLY, wcd->supplies);
	if (ret) {
		dev_err(dev, "Failed to get supplies: err = %d\n", ret);
		return ret;
	}

	return 0;
}

static int wcd9335_power_on_reset(struct wcd9335_codec *wcd)
{
	struct device *dev = wcd->dev;
	int ret;

	ret = regulator_bulk_enable(WCD9335_MAX_SUPPLY, wcd->supplies);
	if (ret) {
		dev_err(dev, "Failed to get supplies: err = %d\n", ret);
		return ret;
	}

	/*
	 * For WCD9335, it takes about 600us for the Vout_A and
	 * Vout_D to be ready after BUCK_SIDO is powered up.
	 * SYS_RST_N shouldn't be pulled high during this time
	 * Toggle the reset line to make sure the reset pulse is
	 * correctly applied
	 */
	usleep_range(600, 650);

	gpio_direction_output(wcd->reset_gpio, 0);
	msleep(20);
	gpio_set_value(wcd->reset_gpio, 1);
	msleep(20);

	return 0;
}

static int wcd9335_bring_up(struct wcd9335_codec *wcd)
{
	struct regmap *rm = wcd->regmap;
	int val, byte0;

	regmap_read(rm, WCD9335_CHIP_TIER_CTRL_EFUSE_VAL_OUT0, &val);
	regmap_read(rm, WCD9335_CHIP_TIER_CTRL_CHIP_ID_BYTE0, &byte0);

	if ((val < 0) || (byte0 < 0)) {
		dev_err(wcd->dev, "WCD9335 CODEC version detection fail!\n");
		return -EINVAL;
	}

	if (byte0 == 0x1) {
		dev_info(wcd->dev, "WCD9335 CODEC version is v2.0\n");
		wcd->version = WCD9335_VERSION_2_0;
		regmap_write(rm, WCD9335_CODEC_RPM_RST_CTL, 0x01);
		regmap_write(rm, WCD9335_SIDO_SIDO_TEST_2, 0x00);
		regmap_write(rm, WCD9335_SIDO_SIDO_CCL_8, 0x6F);
		regmap_write(rm, WCD9335_BIAS_VBG_FINE_ADJ, 0x65);
		regmap_write(rm, WCD9335_CODEC_RPM_PWR_CDC_DIG_HM_CTL, 0x5);
		regmap_write(rm, WCD9335_CODEC_RPM_PWR_CDC_DIG_HM_CTL, 0x7);
		regmap_write(rm, WCD9335_CODEC_RPM_PWR_CDC_DIG_HM_CTL, 0x3);
		regmap_write(rm, WCD9335_CODEC_RPM_RST_CTL, 0x3);
	} else {
		dev_err(wcd->dev, "WCD9335 CODEC version not supported\n");
		return -EINVAL;
	}

	return 0;
}

static int wcd9335_irq_init(struct wcd9335_codec *wcd)
{
	int ret;

	/*
	 * INTR1 consists of all possible interrupt sources Ear OCP,
	 * HPH OCP, MBHC, MAD, VBAT, and SVA
	 * INTR2 is a subset of first interrupt sources MAD, VBAT, and SVA
	 */
	wcd->intr1 = of_irq_get_byname(wcd->dev->of_node, "intr1");
	if (wcd->intr1 < 0) {
		if (wcd->intr1 != -EPROBE_DEFER)
			dev_err(wcd->dev, "Unable to configure IRQ\n");

		return wcd->intr1;
	}

	ret = devm_regmap_add_irq_chip(wcd->dev, wcd->regmap, wcd->intr1,
				 IRQF_TRIGGER_HIGH, 0,
				 &wcd9335_regmap_irq1_chip, &wcd->irq_data);
	if (ret)
		dev_err(wcd->dev, "Failed to register IRQ chip: %d\n", ret);

	return ret;
}

static int wcd9335_slim_probe(struct slim_device *slim)
{
	struct device *dev = &slim->dev;
	struct wcd9335_codec *wcd;
	int ret;

	wcd = devm_kzalloc(dev, sizeof(*wcd), GFP_KERNEL);
	if (!wcd)
		return	-ENOMEM;

	wcd->dev = dev;
	ret = wcd9335_parse_dt(wcd);
	if (ret) {
		dev_err(dev, "Error parsing DT: %d\n", ret);
		return ret;
	}

	ret = wcd9335_power_on_reset(wcd);
	if (ret)
		return ret;

	dev_set_drvdata(dev, wcd);

	return 0;
}

static int wcd9335_slim_status(struct slim_device *sdev,
			       enum slim_device_status status)
{
	struct device *dev = &sdev->dev;
	struct device_node *ifc_dev_np;
	struct wcd9335_codec *wcd;
	int ret;

	wcd = dev_get_drvdata(dev);

	ifc_dev_np = of_parse_phandle(dev->of_node, "slim-ifc-dev", 0);
	if (!ifc_dev_np) {
		dev_err(dev, "No Interface device found\n");
		return -EINVAL;
	}

	wcd->slim = sdev;
	wcd->slim_ifc_dev = of_slim_get_device(sdev->ctrl, ifc_dev_np);
	if (!wcd->slim_ifc_dev) {
		dev_err(dev, "Unable to get SLIM Interface device\n");
		return -EINVAL;
	}

	slim_get_logical_addr(wcd->slim_ifc_dev);

	wcd->regmap = regmap_init_slimbus(sdev, &wcd9335_regmap_config);
	if (IS_ERR(wcd->regmap)) {
		dev_err(dev, "Failed to allocate slim register map\n");
		return PTR_ERR(wcd->regmap);
	}

	wcd->if_regmap = regmap_init_slimbus(wcd->slim_ifc_dev,
						  &wcd9335_ifc_regmap_config);
	if (IS_ERR(wcd->if_regmap)) {
		dev_err(dev, "Failed to allocate ifc register map\n");
		return PTR_ERR(wcd->if_regmap);
	}

	ret = wcd9335_bring_up(wcd);
	if (ret) {
		dev_err(dev, "Failed to bringup WCD9335\n");
		return ret;
	}

	ret = wcd9335_irq_init(wcd);
	if (ret)
		return ret;

	wcd9335_probe(wcd);

	return ret;
}

static const struct slim_device_id wcd9335_slim_id[] = {
	{SLIM_MANF_ID_QCOM, SLIM_PROD_CODE_WCD9335, 0x1, 0x0},
	{}
};
MODULE_DEVICE_TABLE(slim, wcd9335_slim_id);

static struct slim_driver wcd9335_slim_driver = {
	.driver = {
		.name = "wcd9335-slim",
	},
	.probe = wcd9335_slim_probe,
	.device_status = wcd9335_slim_status,
	.id_table = wcd9335_slim_id,
};

module_slim_driver(wcd9335_slim_driver);
MODULE_DESCRIPTION("WCD9335 slim driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("slim:217:1a0:*");
