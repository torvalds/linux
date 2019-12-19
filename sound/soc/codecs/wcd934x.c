// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019, Linaro Limited

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/wcd934x/registers.h>
#include <linux/mfd/wcd934x/wcd934x.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_clk.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/slimbus.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include "wcd-clsh-v2.h"

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

enum {
	AIF1_PB = 0,
	AIF1_CAP,
	AIF2_PB,
	AIF2_CAP,
	AIF3_PB,
	AIF3_CAP,
	AIF4_PB,
	AIF4_VIFEED,
	AIF4_MAD_TX,
	NUM_CODEC_DAIS,
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

enum {
	INTERP_MAIN_PATH,
	INTERP_MIX_PATH,
};

struct interp_sample_rate {
	int sample_rate;
	int rate_val;
};

static struct interp_sample_rate sr_val_tbl[] = {
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

static struct regmap_config wcd934x_ifc_regmap_config = {
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
	int sysclk_users;
	struct mutex sysclk_mutex;
};

#define to_wcd934x_codec(_hw) container_of(_hw, struct wcd934x_codec, hw)

static int wcd934x_set_sido_input_src(struct wcd934x_codec *wcd,
				      int sido_src)
{
	if (sido_src == wcd->sido_input_src)
		return 0;

	if (sido_src == SIDO_SOURCE_INTERNAL) {
		regmap_update_bits(wcd->regmap, WCD934X_ANA_BUCK_CTL,
				   WCD934X_ANA_BUCK_HI_ACCU_EN_MASK, 0);
		usleep_range(100, 110);
		regmap_update_bits(wcd->regmap, WCD934X_ANA_BUCK_CTL,
				   WCD934X_ANA_BUCK_HI_ACCU_PRE_ENX_MASK, 0x0);
		usleep_range(100, 110);
		regmap_update_bits(wcd->regmap, WCD934X_ANA_RCO,
				   WCD934X_ANA_RCO_BG_EN_MASK, 0);
		usleep_range(100, 110);
	} else if (sido_src == SIDO_SOURCE_RCO_BG) {
		regmap_update_bits(wcd->regmap, WCD934X_ANA_RCO,
				   WCD934X_ANA_RCO_BG_EN_MASK,
				   WCD934X_ANA_RCO_BG_ENABLE);
		usleep_range(100, 110);
		regmap_update_bits(wcd->regmap, WCD934X_ANA_BUCK_CTL,
				   WCD934X_ANA_BUCK_PRE_EN1_MASK,
				   WCD934X_ANA_BUCK_PRE_EN1_ENABLE);
		usleep_range(100, 110);
		regmap_update_bits(wcd->regmap, WCD934X_ANA_BUCK_CTL,
				   WCD934X_ANA_BUCK_PRE_EN2_MASK,
				   WCD934X_ANA_BUCK_PRE_EN2_ENABLE);
		usleep_range(100, 110);
		regmap_update_bits(wcd->regmap, WCD934X_ANA_BUCK_CTL,
				   WCD934X_ANA_BUCK_HI_ACCU_EN_MASK,
				   WCD934X_ANA_BUCK_HI_ACCU_ENABLE);
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
	wcd934x_set_sido_input_src(wcd, SIDO_SOURCE_INTERNAL);

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

			cfg0 = snd_soc_component_read32(comp,
					WCD934X_CDC_RX_INP_MUX_RX_INT_CFG0(j));
			cfg1 = snd_soc_component_read32(comp,
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
			val = snd_soc_component_read32(component,
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
	if (ret)
		return ret;

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

		tx_mux_sel = snd_soc_component_read32(comp, tx_port_reg) &
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

		};

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
		};
		break;
	default:
		dev_err(wcd->dev, "Invalid stream type %d\n",
			substream->stream);
		return -EINVAL;
	};

	wcd->dai[dai->id].sconfig.rate = params_rate(params);
	wcd934x_slim_set_hw_params(wcd, &wcd->dai[dai->id], substream->stream);

	return 0;
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
		slim_stream_unprepare(dai_data->sruntime);
		slim_stream_disable(dai_data->sruntime);
		break;
	default:
		break;
	}

	return 0;
}

static int wcd934x_set_channel_map(struct snd_soc_dai *dai,
				   unsigned int tx_num, unsigned int *tx_slot,
				   unsigned int rx_num, unsigned int *rx_slot)
{
	struct wcd934x_codec *wcd;
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

	if (wcd->tx_chs) {
		wcd->num_tx_port = tx_num;
		for (i = 0; i < tx_num; i++) {
			wcd->tx_chs[i].ch_num = tx_slot[i];
			INIT_LIST_HEAD(&wcd->tx_chs[i].list);
		}
	}

	return 0;
}

static int wcd934x_get_channel_map(struct snd_soc_dai *dai,
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

static struct snd_soc_dai_ops wcd934x_dai_ops = {
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
	ret = clk_hw_register(wcd->dev->parent, hw);
	if (ret)
		return ERR_PTR(ret);

	of_clk_add_provider(np, of_clk_src_simple_get, hw->clk);

	return NULL;
}

static int wcd934x_get_micbias_val(struct device *dev, const char *micbias)
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

	return (mv - 1000) / 50;
}

static int wcd934x_init_dmic(struct snd_soc_component *comp)
{
	int vout_ctl_1, vout_ctl_2, vout_ctl_3, vout_ctl_4;
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);
	u32 def_dmic_rate, dmic_clk_drv;

	vout_ctl_1 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias1-microvolt");
	vout_ctl_2 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias2-microvolt");
	vout_ctl_3 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias3-microvolt");
	vout_ctl_4 = wcd934x_get_micbias_val(comp->dev,
					     "qcom,micbias4-microvolt");

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
	return 0;
}

static void wcd934x_comp_remove(struct snd_soc_component *comp)
{
	struct wcd934x_codec *wcd = dev_get_drvdata(comp->dev);

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

static const struct snd_soc_component_driver wcd934x_component_drv = {
	.probe = wcd934x_comp_probe,
	.remove = wcd934x_comp_remove,
	.set_sysclk = wcd934x_comp_set_sysclk,
};

static int wcd934x_codec_parse_data(struct wcd934x_codec *wcd)
{
	struct device *dev = &wcd->sdev->dev;
	struct device_node *ifc_dev_np;

	ifc_dev_np = of_parse_phandle(dev->of_node, "slim-ifc-dev", 0);
	if (!ifc_dev_np) {
		dev_err(dev, "No Interface device found\n");
		return -EINVAL;
	}

	wcd->sidev = of_slim_get_device(wcd->sdev->ctrl, ifc_dev_np);
	if (!wcd->sidev) {
		dev_err(dev, "Unable to get SLIM Interface device\n");
		return -EINVAL;
	}

	slim_get_logical_addr(wcd->sidev);
	wcd->if_regmap = regmap_init_slimbus(wcd->sidev,
				  &wcd934x_ifc_regmap_config);
	if (IS_ERR(wcd->if_regmap)) {
		dev_err(dev, "Failed to allocate ifc register map\n");
		return PTR_ERR(wcd->if_regmap);
	}

	of_property_read_u32(dev->parent->of_node, "qcom,dmic-sample-rate",
			     &wcd->dmic_sample_rate);

	return 0;
}

static int wcd934x_codec_probe(struct platform_device *pdev)
{
	struct wcd934x_ddata *data = dev_get_drvdata(pdev->dev.parent);
	struct wcd934x_codec *wcd;
	struct device *dev = &pdev->dev;
	int ret, irq;

	wcd = devm_kzalloc(&pdev->dev, sizeof(*wcd), GFP_KERNEL);
	if (!wcd)
		return -ENOMEM;

	wcd->dev = dev;
	wcd->regmap = data->regmap;
	wcd->extclk = data->extclk;
	wcd->sdev = to_slim_device(data->dev);
	mutex_init(&wcd->sysclk_mutex);

	ret = wcd934x_codec_parse_data(wcd);
	if (ret) {
		dev_err(wcd->dev, "Failed to get SLIM IRQ\n");
		return ret;
	}

	/* set default rate 9P6MHz */
	regmap_update_bits(wcd->regmap, WCD934X_CODEC_RPM_CLK_MCLK_CFG,
			   WCD934X_CODEC_RPM_CLK_MCLK_CFG_MCLK_MASK,
			   WCD934X_CODEC_RPM_CLK_MCLK_CFG_9P6MHZ);
	memcpy(wcd->rx_chs, wcd934x_rx_chs, sizeof(wcd934x_rx_chs));
	memcpy(wcd->tx_chs, wcd934x_tx_chs, sizeof(wcd934x_tx_chs));

	irq = regmap_irq_get_virq(data->irq_data, WCD934X_IRQ_SLIMBUS);
	if (irq < 0) {
		dev_err(wcd->dev, "Failed to get SLIM IRQ\n");
		return irq;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL,
					wcd934x_slim_irq_handler,
					IRQF_TRIGGER_RISING,
					"slim", wcd);
	if (ret) {
		dev_err(dev, "Failed to request slimbus irq\n");
		return ret;
	}

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

MODULE_ALIAS("platform:wcd934x-codec");
module_platform_driver(wcd934x_codec_driver);
MODULE_DESCRIPTION("WCD934x codec driver");
MODULE_LICENSE("GPL v2");
