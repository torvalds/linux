// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
// Copyright, 2025 Linaro Ltd

#include <linux/component.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc-dapm.h>
#include <sound/soc.h>
#include "pm4125.h"

static struct pm4125_sdw_ch_info pm4125_sdw_rx_ch_info[] = {
	WCD_SDW_CH(PM4125_HPH_L, PM4125_HPH_PORT, BIT(0)),
	WCD_SDW_CH(PM4125_HPH_R, PM4125_HPH_PORT, BIT(1)),
};

static struct pm4125_sdw_ch_info pm4125_sdw_tx_ch_info[] = {
	WCD_SDW_CH(PM4125_ADC1, PM4125_ADC_1_2_DMIC1L_BCS_PORT, BIT(0)),
	WCD_SDW_CH(PM4125_ADC2, PM4125_ADC_1_2_DMIC1L_BCS_PORT, BIT(1)),
};

static struct sdw_dpn_prop pm4125_dpn_prop[PM4125_MAX_SWR_PORTS] = {
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
	}
};

struct device *pm4125_sdw_device_get(struct device_node *np)
{
	return bus_find_device_by_of_node(&sdw_bus_type, np);
}
EXPORT_SYMBOL_GPL(pm4125_sdw_device_get);

int pm4125_sdw_hw_params(struct pm4125_sdw_priv *priv, struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sdw_port_config port_config[PM4125_MAX_SWR_PORTS];
	unsigned long ch_mask;
	int i, j;

	priv->sconfig.ch_count = 1;
	priv->active_ports = 0;
	for (i = 0; i < PM4125_MAX_SWR_PORTS; i++) {
		ch_mask = priv->port_config[i].ch_mask;
		if (!ch_mask)
			continue;

		for_each_set_bit(j, &ch_mask, 4)
			priv->sconfig.ch_count++;

		port_config[priv->active_ports] = priv->port_config[i];
		priv->active_ports++;
	}

	priv->sconfig.bps = 1;
	priv->sconfig.frame_rate = params_rate(params);
	priv->sconfig.direction = priv->is_tx ? SDW_DATA_DIR_TX : SDW_DATA_DIR_RX;
	priv->sconfig.type = SDW_STREAM_PCM;

	return sdw_stream_add_slave(priv->sdev, &priv->sconfig, &port_config[0], priv->active_ports,
				    priv->sruntime);
}
EXPORT_SYMBOL_GPL(pm4125_sdw_hw_params);

static int pm4125_update_status(struct sdw_slave *slave, enum sdw_slave_status status)
{
	struct pm4125_sdw_priv *priv = dev_get_drvdata(&slave->dev);

	if (priv->regmap && status == SDW_SLAVE_ATTACHED) {
		/* Write out any cached changes that happened between probe and attach */
		regcache_cache_only(priv->regmap, false);
		return regcache_sync(priv->regmap);
	}

	return 0;
}

/*
 * Handle Soundwire out-of-band interrupt event by triggering the first irq of the slave_irq
 * irq domain, which then will be handled by the regmap_irq threaded irq.
 * Looping is to ensure no interrupts were missed in the process.
 */
static int pm4125_interrupt_callback(struct sdw_slave *slave, struct sdw_slave_intr_status *status)
{
	struct pm4125_sdw_priv *priv = dev_get_drvdata(&slave->dev);
	struct irq_domain *slave_irq = priv->slave_irq;
	u32 sts1, sts2, sts3;

	do {
		handle_nested_irq(irq_find_mapping(slave_irq, 0));
		regmap_read(priv->regmap, PM4125_DIG_SWR_INTR_STATUS_0, &sts1);
		regmap_read(priv->regmap, PM4125_DIG_SWR_INTR_STATUS_1, &sts2);
		regmap_read(priv->regmap, PM4125_DIG_SWR_INTR_STATUS_2, &sts3);

	} while (sts1 || sts2 || sts3);

	return IRQ_HANDLED;
}

static const struct reg_default pm4125_defaults[] = {
	{ PM4125_ANA_MICBIAS_MICB_1_2_EN,        0x01 },
	{ PM4125_ANA_MICBIAS_MICB_3_EN,          0x00 },
	{ PM4125_ANA_MICBIAS_LDO_1_SETTING,      0x21 },
	{ PM4125_ANA_MICBIAS_LDO_1_CTRL,         0x01 },
	{ PM4125_ANA_TX_AMIC1,                   0x00 },
	{ PM4125_ANA_TX_AMIC2,                   0x00 },
	{ PM4125_ANA_MBHC_MECH,                  0x39 },
	{ PM4125_ANA_MBHC_ELECT,                 0x08 },
	{ PM4125_ANA_MBHC_ZDET,                  0x10 },
	{ PM4125_ANA_MBHC_RESULT_1,              0x00 },
	{ PM4125_ANA_MBHC_RESULT_2,              0x00 },
	{ PM4125_ANA_MBHC_RESULT_3,              0x00 },
	{ PM4125_ANA_MBHC_BTN0_ZDET_VREF1,       0x00 },
	{ PM4125_ANA_MBHC_BTN1_ZDET_VREF2,       0x10 },
	{ PM4125_ANA_MBHC_BTN2_ZDET_VREF3,       0x20 },
	{ PM4125_ANA_MBHC_BTN3_ZDET_DBG_400,     0x30 },
	{ PM4125_ANA_MBHC_BTN4_ZDET_DBG_1400,    0x40 },
	{ PM4125_ANA_MBHC_MICB2_RAMP,            0x00 },
	{ PM4125_ANA_MBHC_CTL_1,                 0x02 },
	{ PM4125_ANA_MBHC_CTL_2,                 0x05 },
	{ PM4125_ANA_MBHC_PLUG_DETECT_CTL,       0xE9 },
	{ PM4125_ANA_MBHC_ZDET_ANA_CTL,          0x0F },
	{ PM4125_ANA_MBHC_ZDET_RAMP_CTL,         0x00 },
	{ PM4125_ANA_MBHC_FSM_STATUS,            0x00 },
	{ PM4125_ANA_MBHC_ADC_RESULT,            0x00 },
	{ PM4125_ANA_MBHC_CTL_CLK,               0x30 },
	{ PM4125_ANA_MBHC_ZDET_CALIB_RESULT,     0x00 },
	{ PM4125_ANA_NCP_EN,                     0x00 },
	{ PM4125_ANA_NCP_VCTRL,                  0xA7 },
	{ PM4125_ANA_HPHPA_CNP_CTL_1,            0x54 },
	{ PM4125_ANA_HPHPA_CNP_CTL_2,            0x2B },
	{ PM4125_ANA_HPHPA_PA_STATUS,            0x00 },
	{ PM4125_ANA_HPHPA_FSM_CLK,              0x12 },
	{ PM4125_ANA_HPHPA_L_GAIN,               0x00 },
	{ PM4125_ANA_HPHPA_R_GAIN,               0x00 },
	{ PM4125_SWR_HPHPA_HD2,                  0x1B },
	{ PM4125_ANA_HPHPA_SPARE_CTL,            0x02 },
	{ PM4125_ANA_SURGE_EN,                   0x38 },
	{ PM4125_ANA_COMBOPA_CTL,                0x35 },
	{ PM4125_ANA_COMBOPA_CTL_4,              0x84 },
	{ PM4125_ANA_COMBOPA_CTL_5,              0x05 },
	{ PM4125_ANA_RXLDO_CTL,                  0x86 },
	{ PM4125_ANA_MBIAS_EN,                   0x00 },
	{ PM4125_DIG_SWR_CHIP_ID0,               0x00 },
	{ PM4125_DIG_SWR_CHIP_ID1,               0x00 },
	{ PM4125_DIG_SWR_CHIP_ID2,               0x0C },
	{ PM4125_DIG_SWR_CHIP_ID3,               0x01 },
	{ PM4125_DIG_SWR_SWR_TX_CLK_RATE,        0x00 },
	{ PM4125_DIG_SWR_CDC_RST_CTL,            0x03 },
	{ PM4125_DIG_SWR_TOP_CLK_CFG,            0x00 },
	{ PM4125_DIG_SWR_CDC_RX_CLK_CTL,         0x00 },
	{ PM4125_DIG_SWR_CDC_TX_CLK_CTL,         0x33 },
	{ PM4125_DIG_SWR_SWR_RST_EN,             0x00 },
	{ PM4125_DIG_SWR_CDC_RX_RST,             0x00 },
	{ PM4125_DIG_SWR_CDC_RX0_CTL,            0xFC },
	{ PM4125_DIG_SWR_CDC_RX1_CTL,            0xFC },
	{ PM4125_DIG_SWR_CDC_TX_ANA_MODE_0_1,    0x00 },
	{ PM4125_DIG_SWR_CDC_COMP_CTL_0,         0x00 },
	{ PM4125_DIG_SWR_CDC_RX_DELAY_CTL,       0x66 },
	{ PM4125_DIG_SWR_CDC_RX_GAIN_0,          0x55 },
	{ PM4125_DIG_SWR_CDC_RX_GAIN_1,          0xA9 },
	{ PM4125_DIG_SWR_CDC_RX_GAIN_CTL,        0x00 },
	{ PM4125_DIG_SWR_CDC_TX0_CTL,            0x68 },
	{ PM4125_DIG_SWR_CDC_TX1_CTL,            0x68 },
	{ PM4125_DIG_SWR_CDC_TX_RST,             0x00 },
	{ PM4125_DIG_SWR_CDC_REQ0_CTL,           0x01 },
	{ PM4125_DIG_SWR_CDC_REQ1_CTL,           0x01 },
	{ PM4125_DIG_SWR_CDC_RST,                0x00 },
	{ PM4125_DIG_SWR_CDC_AMIC_CTL,           0x02 },
	{ PM4125_DIG_SWR_CDC_DMIC_CTL,           0x00 },
	{ PM4125_DIG_SWR_CDC_DMIC1_CTL,          0x00 },
	{ PM4125_DIG_SWR_CDC_DMIC1_RATE,         0x01 },
	{ PM4125_DIG_SWR_PDM_WD_CTL0,            0x00 },
	{ PM4125_DIG_SWR_PDM_WD_CTL1,            0x00 },
	{ PM4125_DIG_SWR_INTR_MODE,              0x00 },
	{ PM4125_DIG_SWR_INTR_MASK_0,            0xFF },
	{ PM4125_DIG_SWR_INTR_MASK_1,            0x7F },
	{ PM4125_DIG_SWR_INTR_MASK_2,            0x0C },
	{ PM4125_DIG_SWR_INTR_STATUS_0,          0x00 },
	{ PM4125_DIG_SWR_INTR_STATUS_1,          0x00 },
	{ PM4125_DIG_SWR_INTR_STATUS_2,          0x00 },
	{ PM4125_DIG_SWR_INTR_CLEAR_0,           0x00 },
	{ PM4125_DIG_SWR_INTR_CLEAR_1,           0x00 },
	{ PM4125_DIG_SWR_INTR_CLEAR_2,           0x00 },
	{ PM4125_DIG_SWR_INTR_LEVEL_0,           0x00 },
	{ PM4125_DIG_SWR_INTR_LEVEL_1,           0x2A },
	{ PM4125_DIG_SWR_INTR_LEVEL_2,           0x00 },
	{ PM4125_DIG_SWR_CDC_CONN_RX0_CTL,       0x00 },
	{ PM4125_DIG_SWR_CDC_CONN_RX1_CTL,       0x00 },
	{ PM4125_DIG_SWR_LOOP_BACK_MODE,         0x00 },
	{ PM4125_DIG_SWR_DRIVE_STRENGTH_0,       0x00 },
	{ PM4125_DIG_SWR_DIG_DEBUG_CTL,          0x00 },
	{ PM4125_DIG_SWR_DIG_DEBUG_EN,           0x00 },
	{ PM4125_DIG_SWR_DEM_BYPASS_DATA0,       0x55 },
	{ PM4125_DIG_SWR_DEM_BYPASS_DATA1,       0x55 },
	{ PM4125_DIG_SWR_DEM_BYPASS_DATA2,       0x55 },
	{ PM4125_DIG_SWR_DEM_BYPASS_DATA3,       0x01 },
};

static bool pm4125_rdwr_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PM4125_ANA_MICBIAS_MICB_1_2_EN:
	case PM4125_ANA_MICBIAS_MICB_3_EN:
	case PM4125_ANA_MICBIAS_LDO_1_SETTING:
	case PM4125_ANA_MICBIAS_LDO_1_CTRL:
	case PM4125_ANA_TX_AMIC1:
	case PM4125_ANA_TX_AMIC2:
	case PM4125_ANA_MBHC_MECH:
	case PM4125_ANA_MBHC_ELECT:
	case PM4125_ANA_MBHC_ZDET:
	case PM4125_ANA_MBHC_BTN0_ZDET_VREF1:
	case PM4125_ANA_MBHC_BTN1_ZDET_VREF2:
	case PM4125_ANA_MBHC_BTN2_ZDET_VREF3:
	case PM4125_ANA_MBHC_BTN3_ZDET_DBG_400:
	case PM4125_ANA_MBHC_BTN4_ZDET_DBG_1400:
	case PM4125_ANA_MBHC_MICB2_RAMP:
	case PM4125_ANA_MBHC_CTL_1:
	case PM4125_ANA_MBHC_CTL_2:
	case PM4125_ANA_MBHC_PLUG_DETECT_CTL:
	case PM4125_ANA_MBHC_ZDET_ANA_CTL:
	case PM4125_ANA_MBHC_ZDET_RAMP_CTL:
	case PM4125_ANA_MBHC_CTL_CLK:
	case PM4125_ANA_NCP_EN:
	case PM4125_ANA_NCP_VCTRL:
	case PM4125_ANA_HPHPA_CNP_CTL_1:
	case PM4125_ANA_HPHPA_CNP_CTL_2:
	case PM4125_ANA_HPHPA_FSM_CLK:
	case PM4125_ANA_HPHPA_L_GAIN:
	case PM4125_ANA_HPHPA_R_GAIN:
	case PM4125_ANA_HPHPA_SPARE_CTL:
	case PM4125_SWR_HPHPA_HD2:
	case PM4125_ANA_SURGE_EN:
	case PM4125_ANA_COMBOPA_CTL:
	case PM4125_ANA_COMBOPA_CTL_4:
	case PM4125_ANA_COMBOPA_CTL_5:
	case PM4125_ANA_RXLDO_CTL:
	case PM4125_ANA_MBIAS_EN:
	case PM4125_DIG_SWR_SWR_TX_CLK_RATE:
	case PM4125_DIG_SWR_CDC_RST_CTL:
	case PM4125_DIG_SWR_TOP_CLK_CFG:
	case PM4125_DIG_SWR_CDC_RX_CLK_CTL:
	case PM4125_DIG_SWR_CDC_TX_CLK_CTL:
	case PM4125_DIG_SWR_SWR_RST_EN:
	case PM4125_DIG_SWR_CDC_RX_RST:
	case PM4125_DIG_SWR_CDC_RX0_CTL:
	case PM4125_DIG_SWR_CDC_RX1_CTL:
	case PM4125_DIG_SWR_CDC_TX_ANA_MODE_0_1:
	case PM4125_DIG_SWR_CDC_COMP_CTL_0:
	case PM4125_DIG_SWR_CDC_RX_DELAY_CTL:
	case PM4125_DIG_SWR_CDC_RX_GAIN_0:
	case PM4125_DIG_SWR_CDC_RX_GAIN_1:
	case PM4125_DIG_SWR_CDC_RX_GAIN_CTL:
	case PM4125_DIG_SWR_CDC_TX0_CTL:
	case PM4125_DIG_SWR_CDC_TX1_CTL:
	case PM4125_DIG_SWR_CDC_TX_RST:
	case PM4125_DIG_SWR_CDC_REQ0_CTL:
	case PM4125_DIG_SWR_CDC_REQ1_CTL:
	case PM4125_DIG_SWR_CDC_RST:
	case PM4125_DIG_SWR_CDC_AMIC_CTL:
	case PM4125_DIG_SWR_CDC_DMIC_CTL:
	case PM4125_DIG_SWR_CDC_DMIC1_CTL:
	case PM4125_DIG_SWR_CDC_DMIC1_RATE:
	case PM4125_DIG_SWR_PDM_WD_CTL0:
	case PM4125_DIG_SWR_PDM_WD_CTL1:
	case PM4125_DIG_SWR_INTR_MODE:
	case PM4125_DIG_SWR_INTR_MASK_0:
	case PM4125_DIG_SWR_INTR_MASK_1:
	case PM4125_DIG_SWR_INTR_MASK_2:
	case PM4125_DIG_SWR_INTR_CLEAR_0:
	case PM4125_DIG_SWR_INTR_CLEAR_1:
	case PM4125_DIG_SWR_INTR_CLEAR_2:
	case PM4125_DIG_SWR_INTR_LEVEL_0:
	case PM4125_DIG_SWR_INTR_LEVEL_1:
	case PM4125_DIG_SWR_INTR_LEVEL_2:
	case PM4125_DIG_SWR_CDC_CONN_RX0_CTL:
	case PM4125_DIG_SWR_CDC_CONN_RX1_CTL:
	case PM4125_DIG_SWR_LOOP_BACK_MODE:
	case PM4125_DIG_SWR_DRIVE_STRENGTH_0:
	case PM4125_DIG_SWR_DIG_DEBUG_CTL:
	case PM4125_DIG_SWR_DIG_DEBUG_EN:
	case PM4125_DIG_SWR_DEM_BYPASS_DATA0:
	case PM4125_DIG_SWR_DEM_BYPASS_DATA1:
	case PM4125_DIG_SWR_DEM_BYPASS_DATA2:
	case PM4125_DIG_SWR_DEM_BYPASS_DATA3:
		return true;
	}

	return false;
}

static bool pm4125_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PM4125_ANA_MBHC_RESULT_1:
	case PM4125_ANA_MBHC_RESULT_2:
	case PM4125_ANA_MBHC_RESULT_3:
	case PM4125_ANA_MBHC_FSM_STATUS:
	case PM4125_ANA_MBHC_ADC_RESULT:
	case PM4125_ANA_MBHC_ZDET_CALIB_RESULT:
	case PM4125_ANA_HPHPA_PA_STATUS:
	case PM4125_DIG_SWR_CHIP_ID0:
	case PM4125_DIG_SWR_CHIP_ID1:
	case PM4125_DIG_SWR_CHIP_ID2:
	case PM4125_DIG_SWR_CHIP_ID3:
	case PM4125_DIG_SWR_INTR_STATUS_0:
	case PM4125_DIG_SWR_INTR_STATUS_1:
	case PM4125_DIG_SWR_INTR_STATUS_2:
		return true;
	}
	return pm4125_rdwr_register(dev, reg);
}

static bool pm4125_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PM4125_ANA_MBHC_RESULT_1:
	case PM4125_ANA_MBHC_RESULT_2:
	case PM4125_ANA_MBHC_RESULT_3:
	case PM4125_ANA_MBHC_FSM_STATUS:
	case PM4125_ANA_MBHC_ADC_RESULT:
	case PM4125_ANA_MBHC_ZDET_CALIB_RESULT:
	case PM4125_ANA_HPHPA_PA_STATUS:
	case PM4125_DIG_SWR_CHIP_ID0:
	case PM4125_DIG_SWR_CHIP_ID1:
	case PM4125_DIG_SWR_CHIP_ID2:
	case PM4125_DIG_SWR_CHIP_ID3:
	case PM4125_DIG_SWR_INTR_STATUS_0:
	case PM4125_DIG_SWR_INTR_STATUS_1:
	case PM4125_DIG_SWR_INTR_STATUS_2:
		return true;
	}

	return false;
}

static const struct regmap_config pm4125_regmap_config = {
	.name = "pm4125_csr",
	.reg_bits = 32,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = pm4125_defaults,
	.num_reg_defaults = ARRAY_SIZE(pm4125_defaults),
	.max_register = PM4125_MAX_REGISTER,
	.readable_reg = pm4125_readable_register,
	.writeable_reg = pm4125_rdwr_register,
	.volatile_reg = pm4125_volatile_register,
};

static const struct sdw_slave_ops pm4125_slave_ops = {
	.update_status = pm4125_update_status,
	.interrupt_callback = pm4125_interrupt_callback,
};

static int pm4125_sdw_component_bind(struct device *dev, struct device *master, void *data)
{
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static void pm4125_sdw_component_unbind(struct device *dev, struct device *master, void *data)
{
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);
}

static const struct component_ops pm4125_sdw_component_ops = {
	.bind = pm4125_sdw_component_bind,
	.unbind = pm4125_sdw_component_unbind,
};

static int pm4125_probe(struct sdw_slave *pdev, const struct sdw_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct pm4125_sdw_priv *priv;
	u8 master_ch_mask[PM4125_MAX_SWR_CH_IDS];
	int master_ch_mask_size = 0;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Port map index starts at 0, however the data port for this codec starts at index 1 */
	if (of_property_present(dev->of_node, "qcom,tx-port-mapping")) {
		priv->is_tx = true;
		ret = of_property_read_u32_array(dev->of_node, "qcom,tx-port-mapping",
						 &pdev->m_port_map[1], PM4125_MAX_TX_SWR_PORTS);
	} else {
		ret = of_property_read_u32_array(dev->of_node, "qcom,rx-port-mapping",
						 &pdev->m_port_map[1], PM4125_MAX_SWR_PORTS);
	}

	if (ret < 0)
		dev_info(dev, "Error getting static port mapping for %s (%d)\n",
			 priv->is_tx ? "TX" : "RX", ret);

	priv->sdev = pdev;
	dev_set_drvdata(dev, priv);

	pdev->prop.scp_int1_mask = SDW_SCP_INT1_IMPL_DEF |
				   SDW_SCP_INT1_BUS_CLASH |
				   SDW_SCP_INT1_PARITY;
	pdev->prop.lane_control_support = true;
	pdev->prop.simple_clk_stop_capable = true;

	memset(master_ch_mask, 0, PM4125_MAX_SWR_CH_IDS);

	if (priv->is_tx) {
		master_ch_mask_size = of_property_count_u8_elems(dev->of_node,
								 "qcom,tx-channel-mapping");

		if (master_ch_mask_size)
			ret = of_property_read_u8_array(dev->of_node, "qcom,tx-channel-mapping",
							master_ch_mask, master_ch_mask_size);
	} else {
		master_ch_mask_size = of_property_count_u8_elems(dev->of_node,
								 "qcom,rx-channel-mapping");

		if (master_ch_mask_size)
			ret = of_property_read_u8_array(dev->of_node, "qcom,rx-channel-mapping",
							master_ch_mask, master_ch_mask_size);
	}

	if (ret < 0)
		dev_info(dev, "Static channel mapping not specified using device channel maps\n");

	if (priv->is_tx) {
		pdev->prop.source_ports = GENMASK(PM4125_MAX_TX_SWR_PORTS, 0);
		pdev->prop.src_dpn_prop = pm4125_dpn_prop;
		priv->ch_info = &pm4125_sdw_tx_ch_info[0];

		for (i = 0; i < master_ch_mask_size; i++)
			priv->ch_info[i].master_ch_mask = PM4125_SWRM_CH_MASK(master_ch_mask[i]);

		pdev->prop.wake_capable = true;

		priv->regmap = devm_regmap_init_sdw(pdev, &pm4125_regmap_config);
		if (IS_ERR(priv->regmap))
			return dev_err_probe(dev, PTR_ERR(priv->regmap), "regmap init failed\n");

		/* Start in cache-only until device is enumerated */
		regcache_cache_only(priv->regmap, true);
	} else {
		pdev->prop.sink_ports = GENMASK(PM4125_MAX_SWR_PORTS - 1, 0);
		pdev->prop.sink_dpn_prop = pm4125_dpn_prop;
		priv->ch_info = &pm4125_sdw_rx_ch_info[0];

		for (i = 0; i < master_ch_mask_size; i++)
			priv->ch_info[i].master_ch_mask = PM4125_SWRM_CH_MASK(master_ch_mask[i]);
	}

	ret = component_add(dev, &pm4125_sdw_component_ops);
	if (ret)
		return ret;

	/* Set suspended until aggregate device is bind */
	pm_runtime_set_suspended(dev);

	return 0;
}

static int pm4125_remove(struct sdw_slave *pdev)
{
	struct device *dev = &pdev->dev;

	component_del(dev, &pm4125_sdw_component_ops);

	return 0;
}

static const struct sdw_device_id pm4125_slave_id[] = {
	SDW_SLAVE_ENTRY(0x0217, 0x10c, 0), /* Soundwire pm4125 RX/TX Device ID */
	{ }
};
MODULE_DEVICE_TABLE(sdw, pm4125_slave_id);

static int __maybe_unused pm4125_sdw_runtime_suspend(struct device *dev)
{
	struct pm4125_sdw_priv *priv = dev_get_drvdata(dev);

	if (priv->regmap) {
		regcache_cache_only(priv->regmap, true);
		regcache_mark_dirty(priv->regmap);
	}

	return 0;
}

static int __maybe_unused pm4125_sdw_runtime_resume(struct device *dev)
{
	struct pm4125_sdw_priv *priv = dev_get_drvdata(dev);

	if (priv->regmap) {
		regcache_cache_only(priv->regmap, false);
		regcache_sync(priv->regmap);
	}

	return 0;
}

static const struct dev_pm_ops pm4125_sdw_pm_ops = {
	SET_RUNTIME_PM_OPS(pm4125_sdw_runtime_suspend, pm4125_sdw_runtime_resume, NULL)
};

static struct sdw_driver pm4125_codec_driver = {
	.probe = pm4125_probe,
	.remove = pm4125_remove,
	.ops = &pm4125_slave_ops,
	.id_table = pm4125_slave_id,
	.driver = {
		.name = "pm4125-codec",
		.pm = &pm4125_sdw_pm_ops,
	}
};
module_sdw_driver(pm4125_codec_driver);

MODULE_DESCRIPTION("PM4125 SDW codec driver");
MODULE_LICENSE("GPL");
