// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011, NVIDIA Corporation.
 */

#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/rfkill.h>
#include <linux/of.h>
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

	if (!blocked && !IS_ERR(rfkill->clk) && !rfkill->clk_enabled) {
		int ret = clk_enable(rfkill->clk);

		if (ret)
			return ret;
	}

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

/* List of DMI matches for devices on which rfkill-gpio should not load,
 * to avoid firmware bugs.
 */
static const struct dmi_system_id rfkill_gpio_deny_table[] = {
	{
		/* Lenovo Yoga Tab 3 Pro YT3-X90, bogus "BCM4752" device in DSDT */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Blade3-10A-001"),
		},
	},
	{ }
};

static int rfkill_gpio_probe(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill;
	struct gpio_desc *gpio;
	const char *name_property;
	const char *type_property;
	const char *type_name;
	int ret;

	if (dmi_check_system(rfkill_gpio_deny_table))
		return -ENODEV;

	rfkill = devm_kzalloc(&pdev->dev, sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	if (dev_of_node(&pdev->dev)) {
		name_property = "label";
		type_property = "radio-type";
	} else {
		name_property = "name";
		type_property = "type";
	}
	device_property_read_string(&pdev->dev, name_property, &rfkill->name);
	device_property_read_string(&pdev->dev, type_property, &type_name);

	if (!rfkill->name)
		rfkill->name = dev_name(&pdev->dev);

	rfkill->type = rfkill_find_type(type_name);

	if (ACPI_HANDLE(&pdev->dev)) {
		ret = rfkill_gpio_acpi_probe(&pdev->dev, rfkill);
		if (ret)
			return ret;
	}

	rfkill->clk = devm_clk_get(&pdev->dev, NULL);

	gpio = devm_gpiod_get_optional(&pdev->dev, "reset", GPIOD_ASIS);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	rfkill->reset_gpio = gpio;

	gpio = devm_gpiod_get_optional(&pdev->dev, "shutdown", GPIOD_ASIS);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	rfkill->shutdown_gpio = gpio;

	/* Make sure at-least one GPIO is defined for this instance */
	if (!rfkill->reset_gpio && !rfkill->shutdown_gpio) {
		dev_err(&pdev->dev, "invalid platform data\n");
		return -EINVAL;
	}

	ret = gpiod_direction_output(rfkill->reset_gpio, true);
	if (ret)
		return ret;

	ret = gpiod_direction_output(rfkill->shutdown_gpio, true);
	if (ret)
		return ret;

	rfkill->rfkill_dev = rfkill_alloc(rfkill->name, &pdev->dev,
					  rfkill->type, &rfkill_gpio_ops,
					  rfkill);
	if (!rfkill->rfkill_dev)
		return -ENOMEM;

	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto err_destroy;

	platform_set_drvdata(pdev, rfkill);

	dev_info(&pdev->dev, "%s device registered.\n", rfkill->name);

	return 0;

err_destroy:
	rfkill_destroy(rfkill->rfkill_dev);

	return ret;
}

static void rfkill_gpio_remove(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill = platform_get_drvdata(pdev);

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id rfkill_acpi_match[] = {
	{ "BCM4752", RFKILL_TYPE_GPS },
	{ "LNV4752", RFKILL_TYPE_GPS },
	{ },
};
MODULE_DEVICE_TABLE(acpi, rfkill_acpi_match);
#endif

static const struct of_device_id rfkill_of_match[] __maybe_unused = {
	{ .compatible = "rfkill-gpio", },
	{ },
};
MODULE_DEVICE_TABLE(of, rfkill_of_match);

static struct platform_driver rfkill_gpio_driver = {
	.probe = rfkill_gpio_probe,
	.remove_new = rfkill_gpio_remove,
	.driver = {
		.name = "rfkill_gpio",
		.acpi_match_table = ACPI_PTR(rfkill_acpi_match),
		.of_match_table = of_match_ptr(rfkill_of_match),
	},
};

module_platform_driver(rfkill_gpio_driver);

MODULE_DESCRIPTION("gpio rfkill");
MODULE_AUTHOR("NVIDIA");
MODULE_LICENSE("GPL");
