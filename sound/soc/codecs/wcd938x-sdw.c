// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/component.h>
#include <linux/pm_runtime.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "wcd938x.h"

#define SWRS_SCP_HOST_CLK_DIV2_CTL_BANK(m) (0xE0 + 0x10 * (m))

static struct wcd938x_sdw_ch_info wcd938x_sdw_rx_ch_info[] = {
	WCD_SDW_CH(WCD938X_HPH_L, WCD938X_HPH_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_HPH_R, WCD938X_HPH_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_CLSH, WCD938X_CLSH_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_COMP_L, WCD938X_COMP_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_COMP_R, WCD938X_COMP_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_LO, WCD938X_LO_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DSD_L, WCD938X_DSD_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DSD_R, WCD938X_DSD_PORT, BIT(1)),
};

static struct wcd938x_sdw_ch_info wcd938x_sdw_tx_ch_info[] = {
	WCD_SDW_CH(WCD938X_ADC1, WCD938X_ADC_1_2_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_ADC2, WCD938X_ADC_1_2_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_ADC3, WCD938X_ADC_3_4_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_ADC4, WCD938X_ADC_3_4_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_DMIC0, WCD938X_DMIC_0_3_MBHC_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DMIC1, WCD938X_DMIC_0_3_MBHC_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_MBHC, WCD938X_DMIC_0_3_MBHC_PORT, BIT(2)),
	WCD_SDW_CH(WCD938X_DMIC2, WCD938X_DMIC_0_3_MBHC_PORT, BIT(2)),
	WCD_SDW_CH(WCD938X_DMIC3, WCD938X_DMIC_0_3_MBHC_PORT, BIT(3)),
	WCD_SDW_CH(WCD938X_DMIC4, WCD938X_DMIC_4_7_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DMIC5, WCD938X_DMIC_4_7_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_DMIC6, WCD938X_DMIC_4_7_PORT, BIT(2)),
	WCD_SDW_CH(WCD938X_DMIC7, WCD938X_DMIC_4_7_PORT, BIT(3)),
};

static struct sdw_dpn_prop wcd938x_dpn_prop[WCD938X_MAX_SWR_PORTS] = {
	{
		.num = 1,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 8,
		.simple_ch_prep_sm = true,
	}, {
		.num = 2,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}, {
		.num = 3,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}, {
		.num = 4,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}, {
		.num = 5,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}
};

struct device *wcd938x_sdw_device_get(struct device_node *np)
{
	return bus_find_device_by_of_node(&sdw_bus_type, np);

}
EXPORT_SYMBOL_GPL(wcd938x_sdw_device_get);

int wcd938x_swr_get_current_bank(struct sdw_slave *sdev)
{
	int bank;

	bank  = sdw_read(sdev, SDW_SCP_CTRL);

	return ((bank & 0x40) ? 1 : 0);
}
EXPORT_SYMBOL_GPL(wcd938x_swr_get_current_bank);

int wcd938x_sdw_hw_params(struct wcd938x_sdw_priv *wcd,
			  struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params,
			  struct snd_soc_dai *dai)
{
	struct sdw_port_config port_config[WCD938X_MAX_SWR_PORTS];
	unsigned long ch_mask;
	int i, j;

	wcd->sconfig.ch_count = 1;
	wcd->active_ports = 0;
	for (i = 0; i < WCD938X_MAX_SWR_PORTS; i++) {
		ch_mask = wcd->port_config[i].ch_mask;

		if (!ch_mask)
			continue;

		for_each_set_bit(j, &ch_mask, 4)
			wcd->sconfig.ch_count++;

		port_config[wcd->active_ports] = wcd->port_config[i];
		wcd->active_ports++;
	}

	wcd->sconfig.bps = 1;
	wcd->sconfig.frame_rate =  params_rate(params);
	if (wcd->is_tx)
		wcd->sconfig.direction = SDW_DATA_DIR_TX;
	else
		wcd->sconfig.direction = SDW_DATA_DIR_RX;

	wcd->sconfig.type = SDW_STREAM_PCM;

	return sdw_stream_add_slave(wcd->sdev, &wcd->sconfig,
				    &port_config[0], wcd->active_ports,
				    wcd->sruntime);
}
EXPORT_SYMBOL_GPL(wcd938x_sdw_hw_params);

int wcd938x_sdw_free(struct wcd938x_sdw_priv *wcd,
		     struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai)
{
	sdw_stream_remove_slave(wcd->sdev, wcd->sruntime);

	return 0;
}
EXPORT_SYMBOL_GPL(wcd938x_sdw_free);

int wcd938x_sdw_set_sdw_stream(struct wcd938x_sdw_priv *wcd,
			       struct snd_soc_dai *dai,
			       void *stream, int direction)
{
	wcd->sruntime = stream;

	return 0;
}
EXPORT_SYMBOL_GPL(wcd938x_sdw_set_sdw_stream);

static int wcd9380_update_status(struct sdw_slave *slave,
				 enum sdw_slave_status status)
{
	struct wcd938x_sdw_priv *wcd = dev_get_drvdata(&slave->dev);

	if (wcd->regmap && (status == SDW_SLAVE_ATTACHED)) {
		/* Write out any cached changes that happened between probe and attach */
		regcache_cache_only(wcd->regmap, false);
		return regcache_sync(wcd->regmap);
	}

	return 0;
}

static int wcd9380_bus_config(struct sdw_slave *slave,
			      struct sdw_bus_params *params)
{
	sdw_write(slave, SWRS_SCP_HOST_CLK_DIV2_CTL_BANK(params->next_bank),  0x01);

	return 0;
}

static int wcd9380_interrupt_callback(struct sdw_slave *slave,
				      struct sdw_slave_intr_status *status)
{
	struct wcd938x_sdw_priv *wcd = dev_get_drvdata(&slave->dev);
	struct irq_domain *slave_irq = wcd->slave_irq;
	u32 sts1, sts2, sts3;

	do {
		handle_nested_irq(irq_find_mapping(slave_irq, 0));
		regmap_read(wcd->regmap, WCD938X_DIGITAL_INTR_STATUS_0, &sts1);
		regmap_read(wcd->regmap, WCD938X_DIGITAL_INTR_STATUS_1, &sts2);
		regmap_read(wcd->regmap, WCD938X_DIGITAL_INTR_STATUS_2, &sts3);

	} while (sts1 || sts2 || sts3);

	return IRQ_HANDLED;
}

static const struct reg_default wcd938x_defaults[] = {
	{WCD938X_ANA_PAGE_REGISTER,                            0x00},
	{WCD938X_ANA_BIAS,                                     0x00},
	{WCD938X_ANA_RX_SUPPLIES,                              0x00},
	{WCD938X_ANA_HPH,                                      0x0C},
	{WCD938X_ANA_EAR,                                      0x00},
	{WCD938X_ANA_EAR_COMPANDER_CTL,                        0x02},
	{WCD938X_ANA_TX_CH1,                                   0x20},
	{WCD938X_ANA_TX_CH2,                                   0x00},
	{WCD938X_ANA_TX_CH3,                                   0x20},
	{WCD938X_ANA_TX_CH4,                                   0x00},
	{WCD938X_ANA_MICB1_MICB2_DSP_EN_LOGIC,                 0x00},
	{WCD938X_ANA_MICB3_DSP_EN_LOGIC,                       0x00},
	{WCD938X_ANA_MBHC_MECH,                                0x39},
	{WCD938X_ANA_MBHC_ELECT,                               0x08},
	{WCD938X_ANA_MBHC_ZDET,                                0x00},
	{WCD938X_ANA_MBHC_RESULT_1,                            0x00},
	{WCD938X_ANA_MBHC_RESULT_2,                            0x00},
	{WCD938X_ANA_MBHC_RESULT_3,                            0x00},
	{WCD938X_ANA_MBHC_BTN0,                                0x00},
	{WCD938X_ANA_MBHC_BTN1,                                0x10},
	{WCD938X_ANA_MBHC_BTN2,                                0x20},
	{WCD938X_ANA_MBHC_BTN3,                                0x30},
	{WCD938X_ANA_MBHC_BTN4,                                0x40},
	{WCD938X_ANA_MBHC_BTN5,                                0x50},
	{WCD938X_ANA_MBHC_BTN6,                                0x60},
	{WCD938X_ANA_MBHC_BTN7,                                0x70},
	{WCD938X_ANA_MICB1,                                    0x10},
	{WCD938X_ANA_MICB2,                                    0x10},
	{WCD938X_ANA_MICB2_RAMP,                               0x00},
	{WCD938X_ANA_MICB3,                                    0x10},
	{WCD938X_ANA_MICB4,                                    0x10},
	{WCD938X_BIAS_CTL,                                     0x2A},
	{WCD938X_BIAS_VBG_FINE_ADJ,                            0x55},
	{WCD938X_LDOL_VDDCX_ADJUST,                            0x01},
	{WCD938X_LDOL_DISABLE_LDOL,                            0x00},
	{WCD938X_MBHC_CTL_CLK,                                 0x00},
	{WCD938X_MBHC_CTL_ANA,                                 0x00},
	{WCD938X_MBHC_CTL_SPARE_1,                             0x00},
	{WCD938X_MBHC_CTL_SPARE_2,                             0x00},
	{WCD938X_MBHC_CTL_BCS,                                 0x00},
	{WCD938X_MBHC_MOISTURE_DET_FSM_STATUS,                 0x00},
	{WCD938X_MBHC_TEST_CTL,                                0x00},
	{WCD938X_LDOH_MODE,                                    0x2B},
	{WCD938X_LDOH_BIAS,                                    0x68},
	{WCD938X_LDOH_STB_LOADS,                               0x00},
	{WCD938X_LDOH_SLOWRAMP,                                0x50},
	{WCD938X_MICB1_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB1_TEST_CTL_2,                             0x00},
	{WCD938X_MICB1_TEST_CTL_3,                             0xA4},
	{WCD938X_MICB2_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB2_TEST_CTL_2,                             0x00},
	{WCD938X_MICB2_TEST_CTL_3,                             0x24},
	{WCD938X_MICB3_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB3_TEST_CTL_2,                             0x00},
	{WCD938X_MICB3_TEST_CTL_3,                             0xA4},
	{WCD938X_MICB4_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB4_TEST_CTL_2,                             0x00},
	{WCD938X_MICB4_TEST_CTL_3,                             0xA4},
	{WCD938X_TX_COM_ADC_VCM,                               0x39},
	{WCD938X_TX_COM_BIAS_ATEST,                            0xE0},
	{WCD938X_TX_COM_SPARE1,                                0x00},
	{WCD938X_TX_COM_SPARE2,                                0x00},
	{WCD938X_TX_COM_TXFE_DIV_CTL,                          0x22},
	{WCD938X_TX_COM_TXFE_DIV_START,                        0x00},
	{WCD938X_TX_COM_SPARE3,                                0x00},
	{WCD938X_TX_COM_SPARE4,                                0x00},
	{WCD938X_TX_1_2_TEST_EN,                               0xCC},
	{WCD938X_TX_1_2_ADC_IB,                                0xE9},
	{WCD938X_TX_1_2_ATEST_REFCTL,                          0x0A},
	{WCD938X_TX_1_2_TEST_CTL,                              0x38},
	{WCD938X_TX_1_2_TEST_BLK_EN1,                          0xFF},
	{WCD938X_TX_1_2_TXFE1_CLKDIV,                          0x00},
	{WCD938X_TX_1_2_SAR2_ERR,                              0x00},
	{WCD938X_TX_1_2_SAR1_ERR,                              0x00},
	{WCD938X_TX_3_4_TEST_EN,                               0xCC},
	{WCD938X_TX_3_4_ADC_IB,                                0xE9},
	{WCD938X_TX_3_4_ATEST_REFCTL,                          0x0A},
	{WCD938X_TX_3_4_TEST_CTL,                              0x38},
	{WCD938X_TX_3_4_TEST_BLK_EN3,                          0xFF},
	{WCD938X_TX_3_4_TXFE3_CLKDIV,                          0x00},
	{WCD938X_TX_3_4_SAR4_ERR,                              0x00},
	{WCD938X_TX_3_4_SAR3_ERR,                              0x00},
	{WCD938X_TX_3_4_TEST_BLK_EN2,                          0xFB},
	{WCD938X_TX_3_4_TXFE2_CLKDIV,                          0x00},
	{WCD938X_TX_3_4_SPARE1,                                0x00},
	{WCD938X_TX_3_4_TEST_BLK_EN4,                          0xFB},
	{WCD938X_TX_3_4_TXFE4_CLKDIV,                          0x00},
	{WCD938X_TX_3_4_SPARE2,                                0x00},
	{WCD938X_CLASSH_MODE_1,                                0x40},
	{WCD938X_CLASSH_MODE_2,                                0x3A},
	{WCD938X_CLASSH_MODE_3,                                0x00},
	{WCD938X_CLASSH_CTRL_VCL_1,                            0x70},
	{WCD938X_CLASSH_CTRL_VCL_2,                            0x82},
	{WCD938X_CLASSH_CTRL_CCL_1,                            0x31},
	{WCD938X_CLASSH_CTRL_CCL_2,                            0x80},
	{WCD938X_CLASSH_CTRL_CCL_3,                            0x80},
	{WCD938X_CLASSH_CTRL_CCL_4,                            0x51},
	{WCD938X_CLASSH_CTRL_CCL_5,                            0x00},
	{WCD938X_CLASSH_BUCK_TMUX_A_D,                         0x00},
	{WCD938X_CLASSH_BUCK_SW_DRV_CNTL,                      0x77},
	{WCD938X_CLASSH_SPARE,                                 0x00},
	{WCD938X_FLYBACK_EN,                                   0x4E},
	{WCD938X_FLYBACK_VNEG_CTRL_1,                          0x0B},
	{WCD938X_FLYBACK_VNEG_CTRL_2,                          0x45},
	{WCD938X_FLYBACK_VNEG_CTRL_3,                          0x74},
	{WCD938X_FLYBACK_VNEG_CTRL_4,                          0x7F},
	{WCD938X_FLYBACK_VNEG_CTRL_5,                          0x83},
	{WCD938X_FLYBACK_VNEG_CTRL_6,                          0x98},
	{WCD938X_FLYBACK_VNEG_CTRL_7,                          0xA9},
	{WCD938X_FLYBACK_VNEG_CTRL_8,                          0x68},
	{WCD938X_FLYBACK_VNEG_CTRL_9,                          0x64},
	{WCD938X_FLYBACK_VNEGDAC_CTRL_1,                       0xED},
	{WCD938X_FLYBACK_VNEGDAC_CTRL_2,                       0xF0},
	{WCD938X_FLYBACK_VNEGDAC_CTRL_3,                       0xA6},
	{WCD938X_FLYBACK_CTRL_1,                               0x65},
	{WCD938X_FLYBACK_TEST_CTL,                             0x00},
	{WCD938X_RX_AUX_SW_CTL,                                0x00},
	{WCD938X_RX_PA_AUX_IN_CONN,                            0x01},
	{WCD938X_RX_TIMER_DIV,                                 0x32},
	{WCD938X_RX_OCP_CTL,                                   0x1F},
	{WCD938X_RX_OCP_COUNT,                                 0x77},
	{WCD938X_RX_BIAS_EAR_DAC,                              0xA0},
	{WCD938X_RX_BIAS_EAR_AMP,                              0xAA},
	{WCD938X_RX_BIAS_HPH_LDO,                              0xA9},
	{WCD938X_RX_BIAS_HPH_PA,                               0xAA},
	{WCD938X_RX_BIAS_HPH_RDACBUFF_CNP2,                    0x8A},
	{WCD938X_RX_BIAS_HPH_RDAC_LDO,                         0x88},
	{WCD938X_RX_BIAS_HPH_CNP1,                             0x82},
	{WCD938X_RX_BIAS_HPH_LOWPOWER,                         0x82},
	{WCD938X_RX_BIAS_AUX_DAC,                              0xA0},
	{WCD938X_RX_BIAS_AUX_AMP,                              0xAA},
	{WCD938X_RX_BIAS_VNEGDAC_BLEEDER,                      0x50},
	{WCD938X_RX_BIAS_MISC,                                 0x00},
	{WCD938X_RX_BIAS_BUCK_RST,                             0x08},
	{WCD938X_RX_BIAS_BUCK_VREF_ERRAMP,                     0x44},
	{WCD938X_RX_BIAS_FLYB_ERRAMP,                          0x40},
	{WCD938X_RX_BIAS_FLYB_BUFF,                            0xAA},
	{WCD938X_RX_BIAS_FLYB_MID_RST,                         0x14},
	{WCD938X_HPH_L_STATUS,                                 0x04},
	{WCD938X_HPH_R_STATUS,                                 0x04},
	{WCD938X_HPH_CNP_EN,                                   0x80},
	{WCD938X_HPH_CNP_WG_CTL,                               0x9A},
	{WCD938X_HPH_CNP_WG_TIME,                              0x14},
	{WCD938X_HPH_OCP_CTL,                                  0x28},
	{WCD938X_HPH_AUTO_CHOP,                                0x16},
	{WCD938X_HPH_CHOP_CTL,                                 0x83},
	{WCD938X_HPH_PA_CTL1,                                  0x46},
	{WCD938X_HPH_PA_CTL2,                                  0x50},
	{WCD938X_HPH_L_EN,                                     0x80},
	{WCD938X_HPH_L_TEST,                                   0xE0},
	{WCD938X_HPH_L_ATEST,                                  0x50},
	{WCD938X_HPH_R_EN,                                     0x80},
	{WCD938X_HPH_R_TEST,                                   0xE0},
	{WCD938X_HPH_R_ATEST,                                  0x54},
	{WCD938X_HPH_RDAC_CLK_CTL1,                            0x99},
	{WCD938X_HPH_RDAC_CLK_CTL2,                            0x9B},
	{WCD938X_HPH_RDAC_LDO_CTL,                             0x33},
	{WCD938X_HPH_RDAC_CHOP_CLK_LP_CTL,                     0x00},
	{WCD938X_HPH_REFBUFF_UHQA_CTL,                         0x68},
	{WCD938X_HPH_REFBUFF_LP_CTL,                           0x0E},
	{WCD938X_HPH_L_DAC_CTL,                                0x20},
	{WCD938X_HPH_R_DAC_CTL,                                0x20},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_COMP_SEL,               0x55},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_EN,                     0x19},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_MISC1,                  0xA0},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_STATUS,                 0x00},
	{WCD938X_EAR_EAR_EN_REG,                               0x22},
	{WCD938X_EAR_EAR_PA_CON,                               0x44},
	{WCD938X_EAR_EAR_SP_CON,                               0xDB},
	{WCD938X_EAR_EAR_DAC_CON,                              0x80},
	{WCD938X_EAR_EAR_CNP_FSM_CON,                          0xB2},
	{WCD938X_EAR_TEST_CTL,                                 0x00},
	{WCD938X_EAR_STATUS_REG_1,                             0x00},
	{WCD938X_EAR_STATUS_REG_2,                             0x08},
	{WCD938X_ANA_NEW_PAGE_REGISTER,                        0x00},
	{WCD938X_HPH_NEW_ANA_HPH2,                             0x00},
	{WCD938X_HPH_NEW_ANA_HPH3,                             0x00},
	{WCD938X_SLEEP_CTL,                                    0x16},
	{WCD938X_SLEEP_WATCHDOG_CTL,                           0x00},
	{WCD938X_MBHC_NEW_ELECT_REM_CLAMP_CTL,                 0x00},
	{WCD938X_MBHC_NEW_CTL_1,                               0x02},
	{WCD938X_MBHC_NEW_CTL_2,                               0x05},
	{WCD938X_MBHC_NEW_PLUG_DETECT_CTL,                     0xE9},
	{WCD938X_MBHC_NEW_ZDET_ANA_CTL,                        0x0F},
	{WCD938X_MBHC_NEW_ZDET_RAMP_CTL,                       0x00},
	{WCD938X_MBHC_NEW_FSM_STATUS,                          0x00},
	{WCD938X_MBHC_NEW_ADC_RESULT,                          0x00},
	{WCD938X_TX_NEW_AMIC_MUX_CFG,                          0x00},
	{WCD938X_AUX_AUXPA,                                    0x00},
	{WCD938X_LDORXTX_MODE,                                 0x0C},
	{WCD938X_LDORXTX_CONFIG,                               0x10},
	{WCD938X_DIE_CRACK_DIE_CRK_DET_EN,                     0x00},
	{WCD938X_DIE_CRACK_DIE_CRK_DET_OUT,                    0x00},
	{WCD938X_HPH_NEW_INT_RDAC_GAIN_CTL,                    0x40},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L,                   0x81},
	{WCD938X_HPH_NEW_INT_RDAC_VREF_CTL,                    0x10},
	{WCD938X_HPH_NEW_INT_RDAC_OVERRIDE_CTL,                0x00},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R,                   0x81},
	{WCD938X_HPH_NEW_INT_PA_MISC1,                         0x22},
	{WCD938X_HPH_NEW_INT_PA_MISC2,                         0x00},
	{WCD938X_HPH_NEW_INT_PA_RDAC_MISC,                     0x00},
	{WCD938X_HPH_NEW_INT_HPH_TIMER1,                       0xFE},
	{WCD938X_HPH_NEW_INT_HPH_TIMER2,                       0x02},
	{WCD938X_HPH_NEW_INT_HPH_TIMER3,                       0x4E},
	{WCD938X_HPH_NEW_INT_HPH_TIMER4,                       0x54},
	{WCD938X_HPH_NEW_INT_PA_RDAC_MISC2,                    0x00},
	{WCD938X_HPH_NEW_INT_PA_RDAC_MISC3,                    0x00},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L_NEW,               0x90},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R_NEW,               0x90},
	{WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_LOHIFI,              0x62},
	{WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_ULP,                 0x01},
	{WCD938X_RX_NEW_INT_HPH_RDAC_LDO_LP,                   0x11},
	{WCD938X_MBHC_NEW_INT_MOISTURE_DET_DC_CTRL,            0x57},
	{WCD938X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL,       0x01},
	{WCD938X_MBHC_NEW_INT_MECH_DET_CURRENT,                0x00},
	{WCD938X_MBHC_NEW_INT_SPARE_2,                         0x00},
	{WCD938X_EAR_INT_NEW_EAR_CHOPPER_CON,                  0xA8},
	{WCD938X_EAR_INT_NEW_CNP_VCM_CON1,                     0x42},
	{WCD938X_EAR_INT_NEW_CNP_VCM_CON2,                     0x22},
	{WCD938X_EAR_INT_NEW_EAR_DYNAMIC_BIAS,                 0x00},
	{WCD938X_AUX_INT_EN_REG,                               0x00},
	{WCD938X_AUX_INT_PA_CTRL,                              0x06},
	{WCD938X_AUX_INT_SP_CTRL,                              0xD2},
	{WCD938X_AUX_INT_DAC_CTRL,                             0x80},
	{WCD938X_AUX_INT_CLK_CTRL,                             0x50},
	{WCD938X_AUX_INT_TEST_CTRL,                            0x00},
	{WCD938X_AUX_INT_STATUS_REG,                           0x00},
	{WCD938X_AUX_INT_MISC,                                 0x00},
	{WCD938X_LDORXTX_INT_BIAS,                             0x6E},
	{WCD938X_LDORXTX_INT_STB_LOADS_DTEST,                  0x50},
	{WCD938X_LDORXTX_INT_TEST0,                            0x1C},
	{WCD938X_LDORXTX_INT_STARTUP_TIMER,                    0xFF},
	{WCD938X_LDORXTX_INT_TEST1,                            0x1F},
	{WCD938X_LDORXTX_INT_STATUS,                           0x00},
	{WCD938X_SLEEP_INT_WATCHDOG_CTL_1,                     0x0A},
	{WCD938X_SLEEP_INT_WATCHDOG_CTL_2,                     0x0A},
	{WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT1,               0x02},
	{WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT2,               0x60},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L2,               0xFF},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L1,               0x7F},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L0,               0x3F},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP1P2M,          0x1F},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP0P6M,          0x0F},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L2L1,          0xD7},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L0,            0xC8},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_ULP,           0xC6},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L2L1,      0xD5},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L0,        0xCA},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_ULP,       0x05},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_L2L1L0,    0xA5},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_ULP,       0x13},
	{WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L2L1,             0x88},
	{WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L0ULP,            0x42},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_L2,                  0xFF},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_L1,                  0x64},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_L0,                  0x64},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_ULP,                 0x77},
	{WCD938X_DIGITAL_PAGE_REGISTER,                        0x00},
	{WCD938X_DIGITAL_CHIP_ID0,                             0x00},
	{WCD938X_DIGITAL_CHIP_ID1,                             0x00},
	{WCD938X_DIGITAL_CHIP_ID2,                             0x0D},
	{WCD938X_DIGITAL_CHIP_ID3,                             0x01},
	{WCD938X_DIGITAL_SWR_TX_CLK_RATE,                      0x00},
	{WCD938X_DIGITAL_CDC_RST_CTL,                          0x03},
	{WCD938X_DIGITAL_TOP_CLK_CFG,                          0x00},
	{WCD938X_DIGITAL_CDC_ANA_CLK_CTL,                      0x00},
	{WCD938X_DIGITAL_CDC_DIG_CLK_CTL,                      0xF0},
	{WCD938X_DIGITAL_SWR_RST_EN,                           0x00},
	{WCD938X_DIGITAL_CDC_PATH_MODE,                        0x55},
	{WCD938X_DIGITAL_CDC_RX_RST,                           0x00},
	{WCD938X_DIGITAL_CDC_RX0_CTL,                          0xFC},
	{WCD938X_DIGITAL_CDC_RX1_CTL,                          0xFC},
	{WCD938X_DIGITAL_CDC_RX2_CTL,                          0xFC},
	{WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1,                  0x00},
	{WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3,                  0x00},
	{WCD938X_DIGITAL_CDC_COMP_CTL_0,                       0x00},
	{WCD938X_DIGITAL_CDC_ANA_TX_CLK_CTL,                   0x1E},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A1_0,                     0x00},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A1_1,                     0x01},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A2_0,                     0x63},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A2_1,                     0x04},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A3_0,                     0xAC},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A3_1,                     0x04},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A4_0,                     0x1A},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A4_1,                     0x03},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A5_0,                     0xBC},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A5_1,                     0x02},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A6_0,                     0xC7},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A7_0,                     0xF8},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_0,                      0x47},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_1,                      0x43},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_2,                      0xB1},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_3,                      0x17},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R1,                       0x4D},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R2,                       0x29},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R3,                       0x34},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R4,                       0x59},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R5,                       0x66},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R6,                       0x87},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R7,                       0x64},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A1_0,                     0x00},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A1_1,                     0x01},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A2_0,                     0x96},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A2_1,                     0x09},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A3_0,                     0xAB},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A3_1,                     0x05},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A4_0,                     0x1C},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A4_1,                     0x02},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A5_0,                     0x17},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A5_1,                     0x02},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A6_0,                     0xAA},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A7_0,                     0xE3},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_0,                      0x69},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_1,                      0x54},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_2,                      0x02},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_3,                      0x15},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R1,                       0xA4},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R2,                       0xB5},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R3,                       0x86},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R4,                       0x85},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R5,                       0xAA},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R6,                       0xE2},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R7,                       0x62},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_RX_0,                    0x55},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_RX_1,                    0xA9},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_0,                   0x3D},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_1,                   0x2E},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_2,                   0x01},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_0,                   0x00},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_1,                   0xFC},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_2,                   0x01},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_CTL,                     0x00},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_CTL,                     0x00},
	{WCD938X_DIGITAL_CDC_EAR_PATH_CTL,                     0x00},
	{WCD938X_DIGITAL_CDC_SWR_CLH,                          0x00},
	{WCD938X_DIGITAL_SWR_CLH_BYP,                          0x00},
	{WCD938X_DIGITAL_CDC_TX0_CTL,                          0x68},
	{WCD938X_DIGITAL_CDC_TX1_CTL,                          0x68},
	{WCD938X_DIGITAL_CDC_TX2_CTL,                          0x68},
	{WCD938X_DIGITAL_CDC_TX_RST,                           0x00},
	{WCD938X_DIGITAL_CDC_REQ_CTL,                          0x01},
	{WCD938X_DIGITAL_CDC_RST,                              0x00},
	{WCD938X_DIGITAL_CDC_AMIC_CTL,                         0x0F},
	{WCD938X_DIGITAL_CDC_DMIC_CTL,                         0x04},
	{WCD938X_DIGITAL_CDC_DMIC1_CTL,                        0x01},
	{WCD938X_DIGITAL_CDC_DMIC2_CTL,                        0x01},
	{WCD938X_DIGITAL_CDC_DMIC3_CTL,                        0x01},
	{WCD938X_DIGITAL_CDC_DMIC4_CTL,                        0x01},
	{WCD938X_DIGITAL_EFUSE_PRG_CTL,                        0x00},
	{WCD938X_DIGITAL_EFUSE_CTL,                            0x2B},
	{WCD938X_DIGITAL_CDC_DMIC_RATE_1_2,                    0x11},
	{WCD938X_DIGITAL_CDC_DMIC_RATE_3_4,                    0x11},
	{WCD938X_DIGITAL_PDM_WD_CTL0,                          0x00},
	{WCD938X_DIGITAL_PDM_WD_CTL1,                          0x00},
	{WCD938X_DIGITAL_PDM_WD_CTL2,                          0x00},
	{WCD938X_DIGITAL_INTR_MODE,                            0x00},
	{WCD938X_DIGITAL_INTR_MASK_0,                          0xFF},
	{WCD938X_DIGITAL_INTR_MASK_1,                          0xFF},
	{WCD938X_DIGITAL_INTR_MASK_2,                          0x3F},
	{WCD938X_DIGITAL_INTR_STATUS_0,                        0x00},
	{WCD938X_DIGITAL_INTR_STATUS_1,                        0x00},
	{WCD938X_DIGITAL_INTR_STATUS_2,                        0x00},
	{WCD938X_DIGITAL_INTR_CLEAR_0,                         0x00},
	{WCD938X_DIGITAL_INTR_CLEAR_1,                         0x00},
	{WCD938X_DIGITAL_INTR_CLEAR_2,                         0x00},
	{WCD938X_DIGITAL_INTR_LEVEL_0,                         0x00},
	{WCD938X_DIGITAL_INTR_LEVEL_1,                         0x00},
	{WCD938X_DIGITAL_INTR_LEVEL_2,                         0x00},
	{WCD938X_DIGITAL_INTR_SET_0,                           0x00},
	{WCD938X_DIGITAL_INTR_SET_1,                           0x00},
	{WCD938X_DIGITAL_INTR_SET_2,                           0x00},
	{WCD938X_DIGITAL_INTR_TEST_0,                          0x00},
	{WCD938X_DIGITAL_INTR_TEST_1,                          0x00},
	{WCD938X_DIGITAL_INTR_TEST_2,                          0x00},
	{WCD938X_DIGITAL_TX_MODE_DBG_EN,                       0x00},
	{WCD938X_DIGITAL_TX_MODE_DBG_0_1,                      0x00},
	{WCD938X_DIGITAL_TX_MODE_DBG_2_3,                      0x00},
	{WCD938X_DIGITAL_LB_IN_SEL_CTL,                        0x00},
	{WCD938X_DIGITAL_LOOP_BACK_MODE,                       0x00},
	{WCD938X_DIGITAL_SWR_DAC_TEST,                         0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_RX_0,                     0x40},
	{WCD938X_DIGITAL_SWR_HM_TEST_TX_0,                     0x40},
	{WCD938X_DIGITAL_SWR_HM_TEST_RX_1,                     0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_TX_1,                     0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_TX_2,                     0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_0,                        0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_1,                        0x00},
	{WCD938X_DIGITAL_PAD_CTL_SWR_0,                        0x8F},
	{WCD938X_DIGITAL_PAD_CTL_SWR_1,                        0x06},
	{WCD938X_DIGITAL_I2C_CTL,                              0x00},
	{WCD938X_DIGITAL_CDC_TX_TANGGU_SW_MODE,                0x00},
	{WCD938X_DIGITAL_EFUSE_TEST_CTL_0,                     0x00},
	{WCD938X_DIGITAL_EFUSE_TEST_CTL_1,                     0x00},
	{WCD938X_DIGITAL_EFUSE_T_DATA_0,                       0x00},
	{WCD938X_DIGITAL_EFUSE_T_DATA_1,                       0x00},
	{WCD938X_DIGITAL_PAD_CTL_PDM_RX0,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_RX1,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_TX0,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_TX1,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_TX2,                      0xF1},
	{WCD938X_DIGITAL_PAD_INP_DIS_0,                        0x00},
	{WCD938X_DIGITAL_PAD_INP_DIS_1,                        0x00},
	{WCD938X_DIGITAL_DRIVE_STRENGTH_0,                     0x00},
	{WCD938X_DIGITAL_DRIVE_STRENGTH_1,                     0x00},
	{WCD938X_DIGITAL_DRIVE_STRENGTH_2,                     0x00},
	{WCD938X_DIGITAL_RX_DATA_EDGE_CTL,                     0x1F},
	{WCD938X_DIGITAL_TX_DATA_EDGE_CTL,                     0x80},
	{WCD938X_DIGITAL_GPIO_MODE,                            0x00},
	{WCD938X_DIGITAL_PIN_CTL_OE,                           0x00},
	{WCD938X_DIGITAL_PIN_CTL_DATA_0,                       0x00},
	{WCD938X_DIGITAL_PIN_CTL_DATA_1,                       0x00},
	{WCD938X_DIGITAL_PIN_STATUS_0,                         0x00},
	{WCD938X_DIGITAL_PIN_STATUS_1,                         0x00},
	{WCD938X_DIGITAL_DIG_DEBUG_CTL,                        0x00},
	{WCD938X_DIGITAL_DIG_DEBUG_EN,                         0x00},
	{WCD938X_DIGITAL_ANA_CSR_DBG_ADD,                      0x00},
	{WCD938X_DIGITAL_ANA_CSR_DBG_CTL,                      0x48},
	{WCD938X_DIGITAL_SSP_DBG,                              0x00},
	{WCD938X_DIGITAL_MODE_STATUS_0,                        0x00},
	{WCD938X_DIGITAL_MODE_STATUS_1,                        0x00},
	{WCD938X_DIGITAL_SPARE_0,                              0x00},
	{WCD938X_DIGITAL_SPARE_1,                              0x00},
	{WCD938X_DIGITAL_SPARE_2,                              0x00},
	{WCD938X_DIGITAL_EFUSE_REG_0,                          0x00},
	{WCD938X_DIGITAL_EFUSE_REG_1,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_2,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_3,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_4,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_5,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_6,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_7,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_8,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_9,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_10,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_11,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_12,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_13,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_14,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_15,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_16,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_17,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_18,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_19,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_20,                         0x0E},
	{WCD938X_DIGITAL_EFUSE_REG_21,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_22,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_23,                         0xF8},
	{WCD938X_DIGITAL_EFUSE_REG_24,                         0x16},
	{WCD938X_DIGITAL_EFUSE_REG_25,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_26,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_27,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_28,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_29,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_30,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_31,                         0x00},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_0,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_1,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_2,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_3,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_4,                      0x88},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA0,                     0x55},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA1,                     0x55},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA2,                     0x55},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA3,                     0x01},
};

static bool wcd938x_rdwr_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WCD938X_ANA_PAGE_REGISTER:
	case WCD938X_ANA_BIAS:
	case WCD938X_ANA_RX_SUPPLIES:
	case WCD938X_ANA_HPH:
	case WCD938X_ANA_EAR:
	case WCD938X_ANA_EAR_COMPANDER_CTL:
	case WCD938X_ANA_TX_CH1:
	case WCD938X_ANA_TX_CH2:
	case WCD938X_ANA_TX_CH3:
	case WCD938X_ANA_TX_CH4:
	case WCD938X_ANA_MICB1_MICB2_DSP_EN_LOGIC:
	case WCD938X_ANA_MICB3_DSP_EN_LOGIC:
	case WCD938X_ANA_MBHC_MECH:
	case WCD938X_ANA_MBHC_ELECT:
	case WCD938X_ANA_MBHC_ZDET:
	case WCD938X_ANA_MBHC_BTN0:
	case WCD938X_ANA_MBHC_BTN1:
	case WCD938X_ANA_MBHC_BTN2:
	case WCD938X_ANA_MBHC_BTN3:
	case WCD938X_ANA_MBHC_BTN4:
	case WCD938X_ANA_MBHC_BTN5:
	case WCD938X_ANA_MBHC_BTN6:
	case WCD938X_ANA_MBHC_BTN7:
	case WCD938X_ANA_MICB1:
	case WCD938X_ANA_MICB2:
	case WCD938X_ANA_MICB2_RAMP:
	case WCD938X_ANA_MICB3:
	case WCD938X_ANA_MICB4:
	case WCD938X_BIAS_CTL:
	case WCD938X_BIAS_VBG_FINE_ADJ:
	case WCD938X_LDOL_VDDCX_ADJUST:
	case WCD938X_LDOL_DISABLE_LDOL:
	case WCD938X_MBHC_CTL_CLK:
	case WCD938X_MBHC_CTL_ANA:
	case WCD938X_MBHC_CTL_SPARE_1:
	case WCD938X_MBHC_CTL_SPARE_2:
	case WCD938X_MBHC_CTL_BCS:
	case WCD938X_MBHC_TEST_CTL:
	case WCD938X_LDOH_MODE:
	case WCD938X_LDOH_BIAS:
	case WCD938X_LDOH_STB_LOADS:
	case WCD938X_LDOH_SLOWRAMP:
	case WCD938X_MICB1_TEST_CTL_1:
	case WCD938X_MICB1_TEST_CTL_2:
	case WCD938X_MICB1_TEST_CTL_3:
	case WCD938X_MICB2_TEST_CTL_1:
	case WCD938X_MICB2_TEST_CTL_2:
	case WCD938X_MICB2_TEST_CTL_3:
	case WCD938X_MICB3_TEST_CTL_1:
	case WCD938X_MICB3_TEST_CTL_2:
	case WCD938X_MICB3_TEST_CTL_3:
	case WCD938X_MICB4_TEST_CTL_1:
	case WCD938X_MICB4_TEST_CTL_2:
	case WCD938X_MICB4_TEST_CTL_3:
	case WCD938X_TX_COM_ADC_VCM:
	case WCD938X_TX_COM_BIAS_ATEST:
	case WCD938X_TX_COM_SPARE1:
	case WCD938X_TX_COM_SPARE2:
	case WCD938X_TX_COM_TXFE_DIV_CTL:
	case WCD938X_TX_COM_TXFE_DIV_START:
	case WCD938X_TX_COM_SPARE3:
	case WCD938X_TX_COM_SPARE4:
	case WCD938X_TX_1_2_TEST_EN:
	case WCD938X_TX_1_2_ADC_IB:
	case WCD938X_TX_1_2_ATEST_REFCTL:
	case WCD938X_TX_1_2_TEST_CTL:
	case WCD938X_TX_1_2_TEST_BLK_EN1:
	case WCD938X_TX_1_2_TXFE1_CLKDIV:
	case WCD938X_TX_3_4_TEST_EN:
	case WCD938X_TX_3_4_ADC_IB:
	case WCD938X_TX_3_4_ATEST_REFCTL:
	case WCD938X_TX_3_4_TEST_CTL:
	case WCD938X_TX_3_4_TEST_BLK_EN3:
	case WCD938X_TX_3_4_TXFE3_CLKDIV:
	case WCD938X_TX_3_4_TEST_BLK_EN2:
	case WCD938X_TX_3_4_TXFE2_CLKDIV:
	case WCD938X_TX_3_4_SPARE1:
	case WCD938X_TX_3_4_TEST_BLK_EN4:
	case WCD938X_TX_3_4_TXFE4_CLKDIV:
	case WCD938X_TX_3_4_SPARE2:
	case WCD938X_CLASSH_MODE_1:
	case WCD938X_CLASSH_MODE_2:
	case WCD938X_CLASSH_MODE_3:
	case WCD938X_CLASSH_CTRL_VCL_1:
	case WCD938X_CLASSH_CTRL_VCL_2:
	case WCD938X_CLASSH_CTRL_CCL_1:
	case WCD938X_CLASSH_CTRL_CCL_2:
	case WCD938X_CLASSH_CTRL_CCL_3:
	case WCD938X_CLASSH_CTRL_CCL_4:
	case WCD938X_CLASSH_CTRL_CCL_5:
	case WCD938X_CLASSH_BUCK_TMUX_A_D:
	case WCD938X_CLASSH_BUCK_SW_DRV_CNTL:
	case WCD938X_CLASSH_SPARE:
	case WCD938X_FLYBACK_EN:
	case WCD938X_FLYBACK_VNEG_CTRL_1:
	case WCD938X_FLYBACK_VNEG_CTRL_2:
	case WCD938X_FLYBACK_VNEG_CTRL_3:
	case WCD938X_FLYBACK_VNEG_CTRL_4:
	case WCD938X_FLYBACK_VNEG_CTRL_5:
	case WCD938X_FLYBACK_VNEG_CTRL_6:
	case WCD938X_FLYBACK_VNEG_CTRL_7:
	case WCD938X_FLYBACK_VNEG_CTRL_8:
	case WCD938X_FLYBACK_VNEG_CTRL_9:
	case WCD938X_FLYBACK_VNEGDAC_CTRL_1:
	case WCD938X_FLYBACK_VNEGDAC_CTRL_2:
	case WCD938X_FLYBACK_VNEGDAC_CTRL_3:
	case WCD938X_FLYBACK_CTRL_1:
	case WCD938X_FLYBACK_TEST_CTL:
	case WCD938X_RX_AUX_SW_CTL:
	case WCD938X_RX_PA_AUX_IN_CONN:
	case WCD938X_RX_TIMER_DIV:
	case WCD938X_RX_OCP_CTL:
	case WCD938X_RX_OCP_COUNT:
	case WCD938X_RX_BIAS_EAR_DAC:
	case WCD938X_RX_BIAS_EAR_AMP:
	case WCD938X_RX_BIAS_HPH_LDO:
	case WCD938X_RX_BIAS_HPH_PA:
	case WCD938X_RX_BIAS_HPH_RDACBUFF_CNP2:
	case WCD938X_RX_BIAS_HPH_RDAC_LDO:
	case WCD938X_RX_BIAS_HPH_CNP1:
	case WCD938X_RX_BIAS_HPH_LOWPOWER:
	case WCD938X_RX_BIAS_AUX_DAC:
	case WCD938X_RX_BIAS_AUX_AMP:
	case WCD938X_RX_BIAS_VNEGDAC_BLEEDER:
	case WCD938X_RX_BIAS_MISC:
	case WCD938X_RX_BIAS_BUCK_RST:
	case WCD938X_RX_BIAS_BUCK_VREF_ERRAMP:
	case WCD938X_RX_BIAS_FLYB_ERRAMP:
	case WCD938X_RX_BIAS_FLYB_BUFF:
	case WCD938X_RX_BIAS_FLYB_MID_RST:
	case WCD938X_HPH_CNP_EN:
	case WCD938X_HPH_CNP_WG_CTL:
	case WCD938X_HPH_CNP_WG_TIME:
	case WCD938X_HPH_OCP_CTL:
	case WCD938X_HPH_AUTO_CHOP:
	case WCD938X_HPH_CHOP_CTL:
	case WCD938X_HPH_PA_CTL1:
	case WCD938X_HPH_PA_CTL2:
	case WCD938X_HPH_L_EN:
	case WCD938X_HPH_L_TEST:
	case WCD938X_HPH_L_ATEST:
	case WCD938X_HPH_R_EN:
	case WCD938X_HPH_R_TEST:
	case WCD938X_HPH_R_ATEST:
	case WCD938X_HPH_RDAC_CLK_CTL1:
	case WCD938X_HPH_RDAC_CLK_CTL2:
	case WCD938X_HPH_RDAC_LDO_CTL:
	case WCD938X_HPH_RDAC_CHOP_CLK_LP_CTL:
	case WCD938X_HPH_REFBUFF_UHQA_CTL:
	case WCD938X_HPH_REFBUFF_LP_CTL:
	case WCD938X_HPH_L_DAC_CTL:
	case WCD938X_HPH_R_DAC_CTL:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_COMP_SEL:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_EN:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_MISC1:
	case WCD938X_EAR_EAR_EN_REG:
	case WCD938X_EAR_EAR_PA_CON:
	case WCD938X_EAR_EAR_SP_CON:
	case WCD938X_EAR_EAR_DAC_CON:
	case WCD938X_EAR_EAR_CNP_FSM_CON:
	case WCD938X_EAR_TEST_CTL:
	case WCD938X_ANA_NEW_PAGE_REGISTER:
	case WCD938X_HPH_NEW_ANA_HPH2:
	case WCD938X_HPH_NEW_ANA_HPH3:
	case WCD938X_SLEEP_CTL:
	case WCD938X_SLEEP_WATCHDOG_CTL:
	case WCD938X_MBHC_NEW_ELECT_REM_CLAMP_CTL:
	case WCD938X_MBHC_NEW_CTL_1:
	case WCD938X_MBHC_NEW_CTL_2:
	case WCD938X_MBHC_NEW_PLUG_DETECT_CTL:
	case WCD938X_MBHC_NEW_ZDET_ANA_CTL:
	case WCD938X_MBHC_NEW_ZDET_RAMP_CTL:
	case WCD938X_TX_NEW_AMIC_MUX_CFG:
	case WCD938X_AUX_AUXPA:
	case WCD938X_LDORXTX_MODE:
	case WCD938X_LDORXTX_CONFIG:
	case WCD938X_DIE_CRACK_DIE_CRK_DET_EN:
	case WCD938X_HPH_NEW_INT_RDAC_GAIN_CTL:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L:
	case WCD938X_HPH_NEW_INT_RDAC_VREF_CTL:
	case WCD938X_HPH_NEW_INT_RDAC_OVERRIDE_CTL:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R:
	case WCD938X_HPH_NEW_INT_PA_MISC1:
	case WCD938X_HPH_NEW_INT_PA_MISC2:
	case WCD938X_HPH_NEW_INT_PA_RDAC_MISC:
	case WCD938X_HPH_NEW_INT_HPH_TIMER1:
	case WCD938X_HPH_NEW_INT_HPH_TIMER2:
	case WCD938X_HPH_NEW_INT_HPH_TIMER3:
	case WCD938X_HPH_NEW_INT_HPH_TIMER4:
	case WCD938X_HPH_NEW_INT_PA_RDAC_MISC2:
	case WCD938X_HPH_NEW_INT_PA_RDAC_MISC3:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L_NEW:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R_NEW:
	case WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_LOHIFI:
	case WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_ULP:
	case WCD938X_RX_NEW_INT_HPH_RDAC_LDO_LP:
	case WCD938X_MBHC_NEW_INT_MOISTURE_DET_DC_CTRL:
	case WCD938X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL:
	case WCD938X_MBHC_NEW_INT_MECH_DET_CURRENT:
	case WCD938X_MBHC_NEW_INT_SPARE_2:
	case WCD938X_EAR_INT_NEW_EAR_CHOPPER_CON:
	case WCD938X_EAR_INT_NEW_CNP_VCM_CON1:
	case WCD938X_EAR_INT_NEW_CNP_VCM_CON2:
	case WCD938X_EAR_INT_NEW_EAR_DYNAMIC_BIAS:
	case WCD938X_AUX_INT_EN_REG:
	case WCD938X_AUX_INT_PA_CTRL:
	case WCD938X_AUX_INT_SP_CTRL:
	case WCD938X_AUX_INT_DAC_CTRL:
	case WCD938X_AUX_INT_CLK_CTRL:
	case WCD938X_AUX_INT_TEST_CTRL:
	case WCD938X_AUX_INT_MISC:
	case WCD938X_LDORXTX_INT_BIAS:
	case WCD938X_LDORXTX_INT_STB_LOADS_DTEST:
	case WCD938X_LDORXTX_INT_TEST0:
	case WCD938X_LDORXTX_INT_STARTUP_TIMER:
	case WCD938X_LDORXTX_INT_TEST1:
	case WCD938X_SLEEP_INT_WATCHDOG_CTL_1:
	case WCD938X_SLEEP_INT_WATCHDOG_CTL_2:
	case WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT1:
	case WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT2:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L2:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L1:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP1P2M:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP0P6M:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L2L1:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_ULP:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L2L1:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_ULP:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_L2L1L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_ULP:
	case WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L2L1:
	case WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L0ULP:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_L2:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_L1:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_L0:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_ULP:
	case WCD938X_DIGITAL_PAGE_REGISTER:
	case WCD938X_DIGITAL_SWR_TX_CLK_RATE:
	case WCD938X_DIGITAL_CDC_RST_CTL:
	case WCD938X_DIGITAL_TOP_CLK_CFG:
	case WCD938X_DIGITAL_CDC_ANA_CLK_CTL:
	case WCD938X_DIGITAL_CDC_DIG_CLK_CTL:
	case WCD938X_DIGITAL_SWR_RST_EN:
	case WCD938X_DIGITAL_CDC_PATH_MODE:
	case WCD938X_DIGITAL_CDC_RX_RST:
	case WCD938X_DIGITAL_CDC_RX0_CTL:
	case WCD938X_DIGITAL_CDC_RX1_CTL:
	case WCD938X_DIGITAL_CDC_RX2_CTL:
	case WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1:
	case WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3:
	case WCD938X_DIGITAL_CDC_COMP_CTL_0:
	case WCD938X_DIGITAL_CDC_ANA_TX_CLK_CTL:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A1_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A1_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A2_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A2_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A3_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A3_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A4_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A4_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A5_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A5_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A6_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A7_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_2:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_3:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R2:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R3:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R4:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R5:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R6:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R7:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A1_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A1_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A2_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A2_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A3_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A3_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A4_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A4_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A5_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A5_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A6_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A7_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_2:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_3:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R2:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R3:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R4:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R5:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R6:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R7:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_RX_0:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_RX_1:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_0:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_1:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_2:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_0:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_1:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_2:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_CTL:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_CTL:
	case WCD938X_DIGITAL_CDC_EAR_PATH_CTL:
	case WCD938X_DIGITAL_CDC_SWR_CLH:
	case WCD938X_DIGITAL_SWR_CLH_BYP:
	case WCD938X_DIGITAL_CDC_TX0_CTL:
	case WCD938X_DIGITAL_CDC_TX1_CTL:
	case WCD938X_DIGITAL_CDC_TX2_CTL:
	case WCD938X_DIGITAL_CDC_TX_RST:
	case WCD938X_DIGITAL_CDC_REQ_CTL:
	case WCD938X_DIGITAL_CDC_RST:
	case WCD938X_DIGITAL_CDC_AMIC_CTL:
	case WCD938X_DIGITAL_CDC_DMIC_CTL:
	case WCD938X_DIGITAL_CDC_DMIC1_CTL:
	case WCD938X_DIGITAL_CDC_DMIC2_CTL:
	case WCD938X_DIGITAL_CDC_DMIC3_CTL:
	case WCD938X_DIGITAL_CDC_DMIC4_CTL:
	case WCD938X_DIGITAL_EFUSE_PRG_CTL:
	case WCD938X_DIGITAL_EFUSE_CTL:
	case WCD938X_DIGITAL_CDC_DMIC_RATE_1_2:
	case WCD938X_DIGITAL_CDC_DMIC_RATE_3_4:
	case WCD938X_DIGITAL_PDM_WD_CTL0:
	case WCD938X_DIGITAL_PDM_WD_CTL1:
	case WCD938X_DIGITAL_PDM_WD_CTL2:
	case WCD938X_DIGITAL_INTR_MODE:
	case WCD938X_DIGITAL_INTR_MASK_0:
	case WCD938X_DIGITAL_INTR_MASK_1:
	case WCD938X_DIGITAL_INTR_MASK_2:
	case WCD938X_DIGITAL_INTR_CLEAR_0:
	case WCD938X_DIGITAL_INTR_CLEAR_1:
	case WCD938X_DIGITAL_INTR_CLEAR_2:
	case WCD938X_DIGITAL_INTR_LEVEL_0:
	case WCD938X_DIGITAL_INTR_LEVEL_1:
	case WCD938X_DIGITAL_INTR_LEVEL_2:
	case WCD938X_DIGITAL_INTR_SET_0:
	case WCD938X_DIGITAL_INTR_SET_1:
	case WCD938X_DIGITAL_INTR_SET_2:
	case WCD938X_DIGITAL_INTR_TEST_0:
	case WCD938X_DIGITAL_INTR_TEST_1:
	case WCD938X_DIGITAL_INTR_TEST_2:
	case WCD938X_DIGITAL_TX_MODE_DBG_EN:
	case WCD938X_DIGITAL_TX_MODE_DBG_0_1:
	case WCD938X_DIGITAL_TX_MODE_DBG_2_3:
	case WCD938X_DIGITAL_LB_IN_SEL_CTL:
	case WCD938X_DIGITAL_LOOP_BACK_MODE:
	case WCD938X_DIGITAL_SWR_DAC_TEST:
	case WCD938X_DIGITAL_SWR_HM_TEST_RX_0:
	case WCD938X_DIGITAL_SWR_HM_TEST_TX_0:
	case WCD938X_DIGITAL_SWR_HM_TEST_RX_1:
	case WCD938X_DIGITAL_SWR_HM_TEST_TX_1:
	case WCD938X_DIGITAL_SWR_HM_TEST_TX_2:
	case WCD938X_DIGITAL_PAD_CTL_SWR_0:
	case WCD938X_DIGITAL_PAD_CTL_SWR_1:
	case WCD938X_DIGITAL_I2C_CTL:
	case WCD938X_DIGITAL_CDC_TX_TANGGU_SW_MODE:
	case WCD938X_DIGITAL_EFUSE_TEST_CTL_0:
	case WCD938X_DIGITAL_EFUSE_TEST_CTL_1:
	case WCD938X_DIGITAL_PAD_CTL_PDM_RX0:
	case WCD938X_DIGITAL_PAD_CTL_PDM_RX1:
	case WCD938X_DIGITAL_PAD_CTL_PDM_TX0:
	case WCD938X_DIGITAL_PAD_CTL_PDM_TX1:
	case WCD938X_DIGITAL_PAD_CTL_PDM_TX2:
	case WCD938X_DIGITAL_PAD_INP_DIS_0:
	case WCD938X_DIGITAL_PAD_INP_DIS_1:
	case WCD938X_DIGITAL_DRIVE_STRENGTH_0:
	case WCD938X_DIGITAL_DRIVE_STRENGTH_1:
	case WCD938X_DIGITAL_DRIVE_STRENGTH_2:
	case WCD938X_DIGITAL_RX_DATA_EDGE_CTL:
	case WCD938X_DIGITAL_TX_DATA_EDGE_CTL:
	case WCD938X_DIGITAL_GPIO_MODE:
	case WCD938X_DIGITAL_PIN_CTL_OE:
	case WCD938X_DIGITAL_PIN_CTL_DATA_0:
	case WCD938X_DIGITAL_PIN_CTL_DATA_1:
	case WCD938X_DIGITAL_DIG_DEBUG_CTL:
	case WCD938X_DIGITAL_DIG_DEBUG_EN:
	case WCD938X_DIGITAL_ANA_CSR_DBG_ADD:
	case WCD938X_DIGITAL_ANA_CSR_DBG_CTL:
	case WCD938X_DIGITAL_SSP_DBG:
	case WCD938X_DIGITAL_SPARE_0:
	case WCD938X_DIGITAL_SPARE_1:
	case WCD938X_DIGITAL_SPARE_2:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_0:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_1:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_2:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_3:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_4:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA0:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA1:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA2:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA3:
		return true;
	}

	return false;
}

static bool wcd938x_readonly_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WCD938X_ANA_MBHC_RESULT_1:
	case WCD938X_ANA_MBHC_RESULT_2:
	case WCD938X_ANA_MBHC_RESULT_3:
	case WCD938X_MBHC_MOISTURE_DET_FSM_STATUS:
	case WCD938X_TX_1_2_SAR2_ERR:
	case WCD938X_TX_1_2_SAR1_ERR:
	case WCD938X_TX_3_4_SAR4_ERR:
	case WCD938X_TX_3_4_SAR3_ERR:
	case WCD938X_HPH_L_STATUS:
	case WCD938X_HPH_R_STATUS:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_STATUS:
	case WCD938X_EAR_STATUS_REG_1:
	case WCD938X_EAR_STATUS_REG_2:
	case WCD938X_MBHC_NEW_FSM_STATUS:
	case WCD938X_MBHC_NEW_ADC_RESULT:
	case WCD938X_DIE_CRACK_DIE_CRK_DET_OUT:
	case WCD938X_AUX_INT_STATUS_REG:
	case WCD938X_LDORXTX_INT_STATUS:
	case WCD938X_DIGITAL_CHIP_ID0:
	case WCD938X_DIGITAL_CHIP_ID1:
	case WCD938X_DIGITAL_CHIP_ID2:
	case WCD938X_DIGITAL_CHIP_ID3:
	case WCD938X_DIGITAL_INTR_STATUS_0:
	case WCD938X_DIGITAL_INTR_STATUS_1:
	case WCD938X_DIGITAL_INTR_STATUS_2:
	case WCD938X_DIGITAL_INTR_CLEAR_0:
	case WCD938X_DIGITAL_INTR_CLEAR_1:
	case WCD938X_DIGITAL_INTR_CLEAR_2:
	case WCD938X_DIGITAL_SWR_HM_TEST_0:
	case WCD938X_DIGITAL_SWR_HM_TEST_1:
	case WCD938X_DIGITAL_EFUSE_T_DATA_0:
	case WCD938X_DIGITAL_EFUSE_T_DATA_1:
	case WCD938X_DIGITAL_PIN_STATUS_0:
	case WCD938X_DIGITAL_PIN_STATUS_1:
	case WCD938X_DIGITAL_MODE_STATUS_0:
	case WCD938X_DIGITAL_MODE_STATUS_1:
	case WCD938X_DIGITAL_EFUSE_REG_0:
	case WCD938X_DIGITAL_EFUSE_REG_1:
	case WCD938X_DIGITAL_EFUSE_REG_2:
	case WCD938X_DIGITAL_EFUSE_REG_3:
	case WCD938X_DIGITAL_EFUSE_REG_4:
	case WCD938X_DIGITAL_EFUSE_REG_5:
	case WCD938X_DIGITAL_EFUSE_REG_6:
	case WCD938X_DIGITAL_EFUSE_REG_7:
	case WCD938X_DIGITAL_EFUSE_REG_8:
	case WCD938X_DIGITAL_EFUSE_REG_9:
	case WCD938X_DIGITAL_EFUSE_REG_10:
	case WCD938X_DIGITAL_EFUSE_REG_11:
	case WCD938X_DIGITAL_EFUSE_REG_12:
	case WCD938X_DIGITAL_EFUSE_REG_13:
	case WCD938X_DIGITAL_EFUSE_REG_14:
	case WCD938X_DIGITAL_EFUSE_REG_15:
	case WCD938X_DIGITAL_EFUSE_REG_16:
	case WCD938X_DIGITAL_EFUSE_REG_17:
	case WCD938X_DIGITAL_EFUSE_REG_18:
	case WCD938X_DIGITAL_EFUSE_REG_19:
	case WCD938X_DIGITAL_EFUSE_REG_20:
	case WCD938X_DIGITAL_EFUSE_REG_21:
	case WCD938X_DIGITAL_EFUSE_REG_22:
	case WCD938X_DIGITAL_EFUSE_REG_23:
	case WCD938X_DIGITAL_EFUSE_REG_24:
	case WCD938X_DIGITAL_EFUSE_REG_25:
	case WCD938X_DIGITAL_EFUSE_REG_26:
	case WCD938X_DIGITAL_EFUSE_REG_27:
	case WCD938X_DIGITAL_EFUSE_REG_28:
	case WCD938X_DIGITAL_EFUSE_REG_29:
	case WCD938X_DIGITAL_EFUSE_REG_30:
	case WCD938X_DIGITAL_EFUSE_REG_31:
		return true;
	}
	return false;
}

static bool wcd938x_readable_register(struct device *dev, unsigned int reg)
{
	bool ret;

	ret = wcd938x_readonly_register(dev, reg);
	if (!ret)
		return wcd938x_rdwr_register(dev, reg);

	return ret;
}

static bool wcd938x_writeable_register(struct device *dev, unsigned int reg)
{
	return wcd938x_rdwr_register(dev, reg);
}

static bool wcd938x_volatile_register(struct device *dev, unsigned int reg)
{
	if (reg <= WCD938X_BASE_ADDRESS)
		return false;

	if (reg == WCD938X_DIGITAL_SWR_TX_CLK_RATE)
		return true;

	if (wcd938x_readonly_register(dev, reg))
		return true;

	return false;
}

static const struct regmap_config wcd938x_regmap_config = {
	.name = "wcd938x_csr",
	.reg_bits = 32,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wcd938x_defaults,
	.num_reg_defaults = ARRAY_SIZE(wcd938x_defaults),
	.max_register = WCD938X_MAX_REGISTER,
	.readable_reg = wcd938x_readable_register,
	.writeable_reg = wcd938x_writeable_register,
	.volatile_reg = wcd938x_volatile_register,
};

static const struct sdw_slave_ops wcd9380_slave_ops = {
	.update_status = wcd9380_update_status,
	.interrupt_callback = wcd9380_interrupt_callback,
	.bus_config = wcd9380_bus_config,
};

static int wcd938x_sdw_component_bind(struct device *dev,
				      struct device *master, void *data)
{
	return 0;
}

static void wcd938x_sdw_component_unbind(struct device *dev,
					 struct device *master, void *data)
{
}

static const struct component_ops wcd938x_sdw_component_ops = {
	.bind   = wcd938x_sdw_component_bind,
	.unbind = wcd938x_sdw_component_unbind,
};

static int wcd9380_probe(struct sdw_slave *pdev,
			 const struct sdw_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct wcd938x_sdw_priv *wcd;
	int ret;

	wcd = devm_kzalloc(dev, sizeof(*wcd), GFP_KERNEL);
	if (!wcd)
		return -ENOMEM;

	/**
	 * Port map index starts with 0, however the data port for this codec
	 * are from index 1
	 */
	if (of_property_read_bool(dev->of_node, "qcom,tx-port-mapping")) {
		wcd->is_tx = true;
		ret = of_property_read_u32_array(dev->of_node, "qcom,tx-port-mapping",
						 &pdev->m_port_map[1],
						 WCD938X_MAX_TX_SWR_PORTS);
	} else {
		ret = of_property_read_u32_array(dev->of_node, "qcom,rx-port-mapping",
						 &pdev->m_port_map[1],
						 WCD938X_MAX_SWR_PORTS);
	}

	if (ret < 0)
		dev_info(dev, "Static Port mapping not specified\n");

	wcd->sdev = pdev;
	dev_set_drvdata(dev, wcd);

	pdev->prop.scp_int1_mask = SDW_SCP_INT1_IMPL_DEF |
					SDW_SCP_INT1_BUS_CLASH |
					SDW_SCP_INT1_PARITY;
	pdev->prop.lane_control_support = true;
	pdev->prop.simple_clk_stop_capable = true;
	if (wcd->is_tx) {
		pdev->prop.source_ports = GENMASK(WCD938X_MAX_SWR_PORTS, 0);
		pdev->prop.src_dpn_prop = wcd938x_dpn_prop;
		wcd->ch_info = &wcd938x_sdw_tx_ch_info[0];
		pdev->prop.wake_capable = true;
	} else {
		pdev->prop.sink_ports = GENMASK(WCD938X_MAX_SWR_PORTS, 0);
		pdev->prop.sink_dpn_prop = wcd938x_dpn_prop;
		wcd->ch_info = &wcd938x_sdw_rx_ch_info[0];
	}

	if (wcd->is_tx) {
		wcd->regmap = devm_regmap_init_sdw(pdev, &wcd938x_regmap_config);
		if (IS_ERR(wcd->regmap))
			return dev_err_probe(dev, PTR_ERR(wcd->regmap),
					     "Regmap init failed\n");

		/* Start in cache-only until device is enumerated */
		regcache_cache_only(wcd->regmap, true);
	}

	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return component_add(dev, &wcd938x_sdw_component_ops);
}

static const struct sdw_device_id wcd9380_slave_id[] = {
	SDW_SLAVE_ENTRY(0x0217, 0x10d, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, wcd9380_slave_id);

static int __maybe_unused wcd938x_sdw_runtime_suspend(struct device *dev)
{
	struct wcd938x_sdw_priv *wcd = dev_get_drvdata(dev);

	if (wcd->regmap) {
		regcache_cache_only(wcd->regmap, true);
		regcache_mark_dirty(wcd->regmap);
	}

	return 0;
}

static int __maybe_unused wcd938x_sdw_runtime_resume(struct device *dev)
{
	struct wcd938x_sdw_priv *wcd = dev_get_drvdata(dev);

	if (wcd->regmap) {
		regcache_cache_only(wcd->regmap, false);
		regcache_sync(wcd->regmap);
	}

	pm_runtime_mark_last_busy(dev);

	return 0;
}

static const struct dev_pm_ops wcd938x_sdw_pm_ops = {
	SET_RUNTIME_PM_OPS(wcd938x_sdw_runtime_suspend, wcd938x_sdw_runtime_resume, NULL)
};


static struct sdw_driver wcd9380_codec_driver = {
	.probe	= wcd9380_probe,
	.ops = &wcd9380_slave_ops,
	.id_table = wcd9380_slave_id,
	.driver = {
		.name	= "wcd9380-codec",
		.pm = &wcd938x_sdw_pm_ops,
	}
};
module_sdw_driver(wcd9380_codec_driver);

MODULE_DESCRIPTION("WCD938X SDW codec driver");
MODULE_LICENSE("GPL");
