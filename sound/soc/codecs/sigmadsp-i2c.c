/*
 * Load Analog Devices SigmaStudio firmware files
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "sigmadsp.h"

static int sigmadsp_write_i2c(void *control_data,
	unsigned int addr, const uint8_t data[], size_t len)
{
	uint8_t *buf;
	int ret;

	buf = kzalloc(2 + len, GFP_KERNEL | GFP_DMA);
	if (!buf)
		return -ENOMEM;

	put_unaligned_be16(addr, buf);
	memcpy(buf + 2, data, len);

	ret = i2c_master_send(control_data, buf, len + 2);

	kfree(buf);

	return ret;
}

/**
 * devm_sigmadsp_init_i2c() - Initialize SigmaDSP instance
 * @client: The parent I2C device
 * @ops: The sigmadsp_ops to use for this instance
 * @firmware_name: Name of the firmware file to load
 *
 * Allocates a SigmaDSP instance and loads the specified firmware file.
 *
 * Returns a pointer to a struct sigmadsp on success, or a PTR_ERR() on error.
 */
struct sigmadsp *devm_sigmadsp_init_i2c(struct i2c_client *client,
	const struct sigmadsp_ops *ops,	const char *firmware_name)
{
	struct sigmadsp *sigmadsp;

	sigmadsp = devm_sigmadsp_init(&client->dev, ops, firmware_name);
	if (IS_ERR(sigmadsp))
		return sigmadsp;

	sigmadsp->control_data = client;
	sigmadsp->write = sigmadsp_write_i2c;

	return sigmadsp;
}
EXPORT_SYMBOL_GPL(devm_sigmadsp_init_i2c);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("SigmaDSP I2C firmware loader");
MODULE_LICENSE("GPL");
