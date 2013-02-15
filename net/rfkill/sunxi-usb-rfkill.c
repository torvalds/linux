/*
 * Copyright (c) 2013 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/slab.h>
#include <plat/sys_config.h>

struct sunxi_usb_rfkill_data {
	unsigned gpio_handle;
	struct rfkill *rfkill_dev;
};

static int sunxi_usb_rfkill_set_block(void *data, bool blocked)
{
	struct sunxi_usb_rfkill_data *rfkill = data;

	return gpio_write_one_pin_value(rfkill->gpio_handle, !blocked,
					"usb_wifi_gpio_power");
}

static const struct rfkill_ops sunxi_usb_rfkill_ops = {
	.set_block = sunxi_usb_rfkill_set_block,
};

static int sunxi_usb_rfkill_probe(struct platform_device *pdev)
{
	struct sunxi_usb_rfkill_data *rfkill;
	int ret = -ENODEV;

	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	rfkill->gpio_handle = gpio_request_ex("usb_wifi_para",
					      "usb_wifi_gpio_power");
	if (!rfkill->gpio_handle)
		goto fail_alloc;

	rfkill->rfkill_dev =
		rfkill_alloc("sunxi usb wifi", &pdev->dev, RFKILL_TYPE_WLAN,
			     &sunxi_usb_rfkill_ops, rfkill);
	if (!rfkill->rfkill_dev)
		goto fail_gpio;

	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto fail_rfkill;

	platform_set_drvdata(pdev, rfkill);
	return 0;

fail_rfkill:
	rfkill_destroy(rfkill->rfkill_dev);
fail_gpio:
	gpio_release(rfkill->gpio_handle, 1);
fail_alloc:
	kfree(rfkill);

	return ret;
}

static int sunxi_usb_rfkill_remove(struct platform_device *pdev)
{
	struct sunxi_usb_rfkill_data *rfkill = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	rfkill_destroy(rfkill->rfkill_dev);
	gpio_release(rfkill->gpio_handle, 1);
	kfree(rfkill);

	return 0;
}

static struct platform_driver sunxi_usb_rfkill_driver = {
	.probe = sunxi_usb_rfkill_probe,
	.remove = __devexit_p(sunxi_usb_rfkill_remove),
	.driver = {
		   .name = "sunxi-usb-rfkill",
		   .owner = THIS_MODULE,
	},
};

static struct platform_device sunxi_usb_rfkill_dev = {
	.name = "sunxi-usb-rfkill",
};

static int __init sunxi_usb_rfkill_init(void)
{
	user_gpio_set_t gpio = { " ", 0 };
	int ret, usb_wifi_used = 0;

	ret = script_parser_fetch("usb_wifi_para", "usb_wifi_used",
				  &usb_wifi_used, 1);
	if (ret != 0 || !usb_wifi_used)
		return -ENODEV;

	ret = script_parser_fetch("usb_wifi_para", "usb_wifi_gpio_power",
				  (int *)&gpio, (sizeof(gpio) >> 2));
	if (ret != 0)
		return -ENODEV;

	ret = platform_device_register(&sunxi_usb_rfkill_dev);
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&sunxi_usb_rfkill_driver);
	if (ret < 0) {
		platform_device_unregister(&sunxi_usb_rfkill_dev);
		return ret;
	}

	return 0;
}

static void __exit sunxi_usb_rfkill_exit(void)
{
	platform_driver_unregister(&sunxi_usb_rfkill_driver);
	platform_device_unregister(&sunxi_usb_rfkill_dev);
}

module_init(sunxi_usb_rfkill_init);
module_exit(sunxi_usb_rfkill_exit);

MODULE_DESCRIPTION("sunxi usb rfkill");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
