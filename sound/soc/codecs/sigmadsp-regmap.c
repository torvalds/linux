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

static int sigma_action_write_regmap(void *control_data,
	const struct sigma_action *sa, size_t len)
{
	return regmap_raw_write(control_data, be16_to_cpu(sa->addr),
		sa->payload, len - 2);
}

int process_sigma_firmware_regmap(struct device *dev, struct regmap *regmap,
	const char *name)
{
	struct sigma_firmware ssfw;

	ssfw.control_data = regmap;
	ssfw.write = sigma_action_write_regmap;

	return _process_sigma_firmware(dev, &ssfw, name);
}
EXPORT_SYMBOL(process_sigma_firmware_regmap);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("SigmaDSP regmap firmware loader");
MODULE_LICENSE("GPL");
