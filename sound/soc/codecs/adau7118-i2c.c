// SPDX-License-Identifier: GPL-2.0
//
// Analog Devices ADAU7118 8 channel PDM-to-I2S/TDM Converter driver over I2C
//
// Copyright 2019 Analog Devices Inc.

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adau7118.h"

static const struct reg_default adau7118_reg_defaults[] = {
	{ ADAU7118_REG_VENDOR_ID, 0x41 },
	{ ADAU7118_REG_DEVICE_ID1, 0x71 },
	{ ADAU7118_REG_DEVICE_ID2, 0x18 },
	{ ADAU7118_REG_REVISION_ID, 0x00 },
	{ ADAU7118_REG_ENABLES, 0x3F },
	{ ADAU7118_REG_DEC_RATIO_CLK_MAP, 0xC0 },
	{ ADAU7118_REG_HPF_CONTROL, 0xD0 },
	{ ADAU7118_REG_SPT_CTRL1, 0x41 },
	{ ADAU7118_REG_SPT_CTRL2, 0x00 },
	{ ADAU7118_REG_SPT_CX(0), 0x01 },
	{ ADAU7118_REG_SPT_CX(1), 0x11 },
	{ ADAU7118_REG_SPT_CX(2), 0x21 },
	{ ADAU7118_REG_SPT_CX(3), 0x31 },
	{ ADAU7118_REG_SPT_CX(4), 0x41 },
	{ ADAU7118_REG_SPT_CX(5), 0x51 },
	{ ADAU7118_REG_SPT_CX(6), 0x61 },
	{ ADAU7118_REG_SPT_CX(7), 0x71 },
	{ ADAU7118_REG_DRIVE_STRENGTH, 0x2a },
	{ ADAU7118_REG_RESET, 0x00 },
};

static const struct regmap_config adau7118_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_defaults = adau7118_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(adau7118_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.max_register = ADAU7118_REG_RESET,
};

static int adau7118_probe_i2c(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct regmap *map;

	map = devm_regmap_init_i2c(i2c, &adau7118_regmap_config);
	if (IS_ERR(map)) {
		dev_err(&i2c->dev, "Failed to init regmap %ld\n", PTR_ERR(map));
		return PTR_ERR(map);
	}

	return adau7118_probe(&i2c->dev, map, false);
}

static const struct of_device_id adau7118_of_match[] = {
	{ .compatible = "adi,adau7118" },
	{}
};
MODULE_DEVICE_TABLE(of, adau7118_of_match);

static const struct i2c_device_id adau7118_id[] = {
	{"adau7118", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, adau7118_id);

static struct i2c_driver adau7118_driver = {
	.driver = {
		.name = "adau7118",
		.of_match_table = adau7118_of_match,
	},
	.probe = adau7118_probe_i2c,
	.id_table = adau7118_id,
};
module_i2c_driver(adau7118_driver);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("ADAU7118 8 channel PDM-to-I2S/TDM Converter driver over I2C");
MODULE_LICENSE("GPL");
