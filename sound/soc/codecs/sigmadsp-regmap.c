/*
 * Load Analog Devices SigmaStudio firmware files
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/regmap.h>
#include <linux/export.h>
#include <linux/module.h>

#include "sigmadsp.h"

static int sigmadsp_write_regmap(void *control_data,
	unsigned int addr, const uint8_t data[], size_t len)
{
	return regmap_raw_write(control_data, addr,
		data, len);
}

static int sigmadsp_read_regmap(void *control_data,
	unsigned int addr, uint8_t data[], size_t len)
{
	return regmap_raw_read(control_data, addr,
		data, len);
}

/**
 * devm_sigmadsp_init_i2c() - Initialize SigmaDSP instance
 * @dev: The parent device
 * @regmap: Regmap instance to use
 * @ops: The sigmadsp_ops to use for this instance
 * @firmware_name: Name of the firmware file to load
 *
 * Allocates a SigmaDSP instance and loads the specified firmware file.
 *
 * Returns a pointer to a struct sigmadsp on success, or a PTR_ERR() on error.
 */
struct sigmadsp *devm_sigmadsp_init_regmap(struct device *dev,
	struct regmap *regmap, const struct sigmadsp_ops *ops,
	const char *firmware_name)
{
	struct sigmadsp *sigmadsp;

	sigmadsp = devm_sigmadsp_init(dev, ops, firmware_name);
	if (IS_ERR(sigmadsp))
		return sigmadsp;

	sigmadsp->control_data = regmap;
	sigmadsp->write = sigmadsp_write_regmap;
	sigmadsp->read = sigmadsp_read_regmap;

	return sigmadsp;
}
EXPORT_SYMBOL_GPL(devm_sigmadsp_init_regmap);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("SigmaDSP regmap firmware loader");
MODULE_LICENSE("GPL");
