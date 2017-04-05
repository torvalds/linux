/*
 * Driver for CX2081X voice capture IC.
 *
 * Copyright: Conexant Systems.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include "cx20810_config.h"

static int i2c_send_array(struct i2c_client *client,
			  const char *buf, int length)
{
	int i, nwrite;
	struct device *dev = &client->dev;

	for (i = 0; i < (length / 2); i++) {
		nwrite = i2c_master_send(client, buf + i * 2, 2);
		if (nwrite != 2) {
			dev_err(dev, "i2c send configs error!/n");
			return -1;
		}
	}
	return 0;
}

static int cx20810_set_mode(struct device *dev, int mode, int index)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;
	char *param;
	int length;

	switch (mode) {
	case CX20810_NORMAL_MODE:
		param = codec_config_param_normal_mode;
		length = sizeof(codec_config_param_normal_mode);
		break;
	case CX20810_NORMAL_MODE2:
		param = codec_config_param_normal_mode2;
		length = sizeof(codec_config_param_normal_mode2);
		break;
	case CX20810_NORMAL_MODE_SIMPLE:
		param = codec_config_param_normal_mode_simple;
		length = sizeof(codec_config_param_normal_mode_simple);
		break;
	case CX20810_48K_16BIT_MODE:
		param = codec_config_param_48k_16bit_mode;
		length = sizeof(codec_config_param_48k_16bit_mode);
		break;
	case CX20810_96K_16BIT_MODE:
		param = codec_config_param_96k_16bit_mode;
		length = sizeof(codec_config_param_96k_16bit_mode);
		break;
	case CX20810_NIRMAL_MODE_CODEC3:
		param = codec3_config_param_normal_mode;
		length = sizeof(codec3_config_param_normal_mode);
		break;
	case CX20810_NIRMAL_MODE_CODEC3_SIMPLE:
		param = codec3_config_param_normal_mode_simple;
		length = sizeof(codec3_config_param_normal_mode_simple);
		break;
	default:
		dev_err(dev, "Illegal mode.\n");
		return 0;
	}

	ret = i2c_send_array(client,	param, length);
	if (ret != 0) {
		dev_err(dev, "cx2081x send configs failed!\n");
		return -1;
	}

	return 0;
}

/*  initial cx20810 */
static void cx20810_init(struct device *dev, int index, int mode)
{
	if (cx20810_set_mode(dev, mode, index) < 0)
		dev_err(dev, "cx20810 set mode fail.\n");
}

static const struct of_device_id of_cx2081x_match[] = {
	{ .compatible = "conexant,cx20810" },
	{ .compatible = "conexant,cx20811" },
	{},
};

MODULE_DEVICE_TABLE(of, of_cx2081x_match);

static int cx2081x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	const struct of_device_id *match;
	struct device *dev = &client->dev;
	struct gpio_desc *reset_gpio;

	if (dev->of_node) {
		match = of_match_device(of_cx2081x_match, dev);
		if (!match) {
			dev_err(dev, "Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	reset_gpio = gpiod_get_optional(dev,
					"reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpio)) {
		int error = PTR_ERR(reset_gpio);

		dev_err(dev, "Failed to request RESET gpio: %d\n", error);
	} else {
		gpiod_set_value(reset_gpio, 0);
		mdelay(10);
		gpiod_set_value(reset_gpio, 1);
		/*
		 * we found there are possibilities download configs through
		 * i2c failed, some delay can fix it.
		 */
		mdelay(10);
	}

	cx20810_init(dev, 0, CX20810_NORMAL_MODE);
	if (!IS_ERR(reset_gpio))
		gpiod_put(reset_gpio);

	return 0;
}

static const struct i2c_device_id cx2081x_id[] = {
	{"cx20810"},
	{"cx20811"},
	{}
};

MODULE_DEVICE_TABLE(i2c, cx20810_id);

static struct i2c_driver cx2081x_driver = {
	.driver         = {
		.name   = "cx2081x",
		.of_match_table = of_cx2081x_match,
	},
	.probe          = cx2081x_probe,
	.id_table       = cx2081x_id,
};

module_i2c_driver(cx2081x_driver);

MODULE_AUTHOR("Timothy");
MODULE_DESCRIPTION("I2C device cx20810 loader");
MODULE_LICENSE("GPL");
