/*
 * Load Analog Devices SigmaStudio firmware files
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/i2c.h>
#include <linux/export.h>
#include <linux/module.h>

#include "sigmadsp.h"

static int sigma_action_write_i2c(void *control_data,
	const struct sigma_action *sa, size_t len)
{
	return i2c_master_send(control_data, (const unsigned char *)&sa->addr,
		len);
}

int process_sigma_firmware(struct i2c_client *client, const char *name)
{
	struct sigma_firmware ssfw;

	ssfw.control_data = client;
	ssfw.write = sigma_action_write_i2c;

	return _process_sigma_firmware(&client->dev, &ssfw, name);
}
EXPORT_SYMBOL(process_sigma_firmware);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("SigmaDSP I2C firmware loader");
MODULE_LICENSE("GPL");
