// SPDX-License-Identifier: GPL-2.0
//
// loongson_i2s_pci.c -- Loongson I2S controller driver
//
// Copyright (C) 2023 Loongson Technology Corporation Limited
// Author: Yingkun Meng <mengyingkun@loongson.cn>
//

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <sound/soc.h>
#include "loongson_i2s.h"
#include "loongson_dma.h"

#define DRIVER_NAME "loongson-i2s-pci"

static bool loongson_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LS_I2S_CFG:
	case LS_I2S_CTRL:
	case LS_I2S_RX_DATA:
	case LS_I2S_TX_DATA:
	case LS_I2S_CFG1:
		return true;
	default:
		return false;
	};
}

static bool loongson_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LS_I2S_VER:
	case LS_I2S_CFG:
	case LS_I2S_CTRL:
	case LS_I2S_RX_DATA:
	case LS_I2S_TX_DATA:
	case LS_I2S_CFG1:
		return true;
	default:
		return false;
	};
}

static bool loongson_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LS_I2S_CFG:
	case LS_I2S_CTRL:
	case LS_I2S_RX_DATA:
	case LS_I2S_TX_DATA:
	case LS_I2S_CFG1:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config loongson_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = LS_I2S_CFG1,
	.writeable_reg = loongson_i2s_wr_reg,
	.readable_reg = loongson_i2s_rd_reg,
	.volatile_reg = loongson_i2s_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

static int loongson_i2s_pci_probe(struct pci_dev *pdev,
				  const struct pci_device_id *pid)
{
	const struct fwnode_handle *fwnode = pdev->dev.fwnode;
	struct loongson_dma_data *tx_data, *rx_data;
	struct device *dev = &pdev->dev;
	struct loongson_i2s *i2s;
	int ret;

	if (pcim_enable_device(pdev)) {
		dev_err(dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->rev_id = pdev->revision;
	i2s->dev = dev;
	pci_set_drvdata(pdev, i2s);

	i2s->reg_base = pcim_iomap_region(pdev, 0, DRIVER_NAME);
	if (IS_ERR(i2s->reg_base)) {
		dev_err(dev, "iomap_region failed\n");
		return PTR_ERR(i2s->reg_base);
	}

	i2s->regmap = devm_regmap_init_mmio(dev, i2s->reg_base,
					    &loongson_i2s_regmap_config);
	if (IS_ERR(i2s->regmap))
		return dev_err_probe(dev, PTR_ERR(i2s->regmap), "regmap_init_mmio failed\n");

	tx_data = &i2s->tx_dma_data;
	rx_data = &i2s->rx_dma_data;

	tx_data->dev_addr = pci_resource_start(pdev, 0) + LS_I2S_TX_DATA;
	tx_data->order_addr = i2s->reg_base + LS_I2S_TX_ORDER;

	rx_data->dev_addr = pci_resource_start(pdev, 0) + LS_I2S_RX_DATA;
	rx_data->order_addr = i2s->reg_base + LS_I2S_RX_ORDER;

	tx_data->irq = fwnode_irq_get_byname(fwnode, "tx");
	if (tx_data->irq < 0)
		return dev_err_probe(dev, tx_data->irq, "dma tx irq invalid\n");

	rx_data->irq = fwnode_irq_get_byname(fwnode, "rx");
	if (rx_data->irq < 0)
		return dev_err_probe(dev, rx_data->irq, "dma rx irq invalid\n");

	ret = device_property_read_u32(dev, "clock-frequency", &i2s->clk_rate);
	if (ret)
		return dev_err_probe(dev, ret, "clock-frequency property invalid\n");

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	if (i2s->rev_id == 1) {
		regmap_write(i2s->regmap, LS_I2S_CTRL, I2S_CTRL_RESET);
		udelay(200);
	}

	ret = devm_snd_soc_register_component(dev, &loongson_i2s_component,
					      &loongson_i2s_dai, 1);
	if (ret)
		return dev_err_probe(dev, ret, "register DAI failed\n");

	return 0;
}

static const struct pci_device_id loongson_i2s_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_LOONGSON, 0x7a27) },
	{ },
};
MODULE_DEVICE_TABLE(pci, loongson_i2s_ids);

static struct pci_driver loongson_i2s_driver = {
	.name = DRIVER_NAME,
	.id_table = loongson_i2s_ids,
	.probe = loongson_i2s_pci_probe,
	.driver = {
		.pm = pm_sleep_ptr(&loongson_i2s_pm),
	},
};
module_pci_driver(loongson_i2s_driver);

MODULE_DESCRIPTION("Loongson I2S Master Mode ASoC Driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_LICENSE("GPL");
