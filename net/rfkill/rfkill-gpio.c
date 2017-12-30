/*
 * Copyright (c) 2011, NVIDIA Corporation.
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>

struct rfkill_gpio_data {
	const char		*name;
	enum rfkill_type	type;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*shutdown_gpio;

	struct rfkill		*rfkill_dev;
	struct clk		*clk;

	bool			clk_enabled;
};

static int rfkill_gpio_set_power(void *data, bool blocked)
{
	struct rfkill_gpio_data *rfkill = data;

	if (!blocked && !IS_ERR(rfkill->clk) && !rfkill->clk_enabled)
		clk_enable(rfkill->clk);

	gpiod_set_value_cansleep(rfkill->shutdown_gpio, !blocked);
	gpiod_set_value_cansleep(rfkill->reset_gpio, !blocked);

	if (blocked && !IS_ERR(rfkill->clk) && rfkill->clk_enabled)
		clk_disable(rfkill->clk);

	rfkill->clk_enabled = !blocked;

	return 0;
}

static const struct rfkill_ops rfkill_gpio_ops = {
	.set_block = rfkill_gpio_set_power,
};

static const struct acpi_gpio_params reset_gpios = { 0, 0, false };
static const struct acpi_gpio_params shutdown_gpios = { 1, 0, false };

static const struct acpi_gpio_mapping acpi_rfkill_default_gpios[] = {
	{ "reset-gpios", &reset_gpios, 1 },
	{ "shutdown-gpios", &shutdown_gpios, 1 },
	{ },
};

static int rfkill_gpio_acpi_probe(struct device *dev,
				  struct rfkill_gpio_data *rfkill)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	rfkill->type = (unsigned)id->driver_data;

	return devm_acpi_dev_add_driver_gpios(dev, acpi_rfkill_default_gpios);
}

static int rfkill_gpio_probe(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill;
	struct gpio_desc *gpio;
	const char *type_name;
	int ret;

	rfkill = devm_kzalloc(&pdev->dev, sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	device_property_read_string(&pdev->dev, "name", &rfkill->name);
	device_property_read_string(&pdev->dev, "type", &type_name);

	if (!rfkill->name)
		rfkill->name = dev_name(&pdev->dev);

	rfkill->type = rfkill_find_type(type_name);

	if (ACPI_HANDLE(&pdev->dev)) {
		ret = rfkill_gpio_acpi_probe(&pdev->dev, rfkill);
		if (ret)
			return ret;
	}

	rfkill->clk = devm_clk_get(&pdev->dev, NULL);

	gpio = devm_gpiod_get_optional(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	rfkill->reset_gpio = gpio;

	gpio = devm_gpiod_get_optional(&pdev->dev, "shutdown", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	rfkill->shutdown_gpio = gpio;

	/* Make sure at-least one GPIO is defined for this instance */
	if (!rfkill->reset_gpio && !rfkill->shutdown_gpio) {
		dev_err(&pdev->dev, "invalid platform data\n");
		return -EINVAL;
	}

	rfkill->rfkill_dev = rfkill_alloc(rfkill->name, &pdev->dev,
					  rfkill->type, &rfkill_gpio_ops,
					  rfkill);
	if (!rfkill->rfkill_dev)
		return -ENOMEM;

	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, rfkill);

	dev_info(&pdev->dev, "%s device registered.\n", rfkill->name);

	return 0;
}

static int rfkill_gpio_remove(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill = platform_get_drvdata(pdev);

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id rfkill_acpi_match[] = {
	{ "BCM4752", RFKILL_TYPE_GPS },
	{ "LNV4752", RFKILL_TYPE_GPS },
	{ },
};
MODULE_DEVICE_TABLE(acpi, rfkill_acpi_match);
#endif

static struct platform_driver rfkill_gpio_driver = {
	.probe = rfkill_gpio_probe,
	.remove = rfkill_gpio_remove,
	.driver = {
		.name = "rfkill_gpio",
		.acpi_match_table = ACPI_PTR(rfkill_acpi_match),
	},
};

module_platform_driver(rfkill_gpio_driver);

MODULE_DESCRIPTION("gpio rfkill");
MODULE_AUTHOR("NVIDIA");
MODULE_LICENSE("GPL");
