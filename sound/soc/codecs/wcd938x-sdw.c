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
	struct regmap *regmap = dev_get_regmap(&slave->dev, NULL);
	u32 sts1, sts2, sts3;

	do {
		handle_nested_irq(irq_find_mapping(slave_irq, 0));
		regmap_read(regmap, WCD938X_DIGITAL_INTR_STATUS_0, &sts1);
		regmap_read(regmap, WCD938X_DIGITAL_INTR_STATUS_1, &sts2);
		regmap_read(regmap, WCD938X_DIGITAL_INTR_STATUS_2, &sts3);

	} while (sts1 || sts2 || sts3);

	return IRQ_HANDLED;
}

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
	struct regmap *regmap = dev_get_regmap(dev, NULL);

	if (regmap) {
		regcache_cache_only(regmap, true);
		regcache_mark_dirty(regmap);
	}
	return 0;
}

static int __maybe_unused wcd938x_sdw_runtime_resume(struct device *dev)
{
	struct regmap *regmap = dev_get_regmap(dev, NULL);

	if (regmap) {
		regcache_cache_only(regmap, false);
		regcache_sync(regmap);
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
