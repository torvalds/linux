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
	int intr1;
	int reset_gpio;
	struct regulator_bulk_data supplies[WCD9335_MAX_SUPPLY];
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
