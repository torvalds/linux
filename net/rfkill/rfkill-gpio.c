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

#include <linux/rfkill-gpio.h>

enum rfkill_gpio_clk_state {
	UNSPECIFIED = 0,
	PWR_ENABLED,
	PWR_DISABLED
};

#define PWR_CLK_SET(_RF, _EN) \
	((_RF)->pwr_clk_enabled = (!(_EN) ? PWR_ENABLED : PWR_DISABLED))
#define PWR_CLK_ENABLED(_RF) ((_RF)->pwr_clk_enabled == PWR_ENABLED)
#define PWR_CLK_DISABLED(_RF) ((_RF)->pwr_clk_enabled != PWR_ENABLED)

struct rfkill_gpio_data {
	struct rfkill_gpio_platform_data	*pdata;
	struct rfkill				*rfkill_dev;
	char					*reset_name;
	char					*shutdown_name;
	enum rfkill_gpio_clk_state		pwr_clk_enabled;
	struct clk				*pwr_clk;
};

static int rfkill_gpio_set_power(void *data, bool blocked)
{
	struct rfkill_gpio_data *rfkill = data;

	if (blocked) {
		if (gpio_is_valid(rfkill->pdata->shutdown_gpio))
			gpio_direction_output(rfkill->pdata->shutdown_gpio, 0);
		if (gpio_is_valid(rfkill->pdata->reset_gpio))
			gpio_direction_output(rfkill->pdata->reset_gpio, 0);
		if (rfkill->pwr_clk && PWR_CLK_ENABLED(rfkill))
			clk_disable(rfkill->pwr_clk);
	} else {
		if (rfkill->pwr_clk && PWR_CLK_DISABLED(rfkill))
			clk_enable(rfkill->pwr_clk);
		if (gpio_is_valid(rfkill->pdata->reset_gpio))
			gpio_direction_output(rfkill->pdata->reset_gpio, 1);
		if (gpio_is_valid(rfkill->pdata->shutdown_gpio))
			gpio_direction_output(rfkill->pdata->shutdown_gpio, 1);
	}

	if (rfkill->pwr_clk)
		PWR_CLK_SET(rfkill, blocked);

	return 0;
}

static const struct rfkill_ops rfkill_gpio_ops = {
	.set_block = rfkill_gpio_set_power,
};

static int rfkill_gpio_probe(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill;
	struct rfkill_gpio_platform_data *pdata = pdev->dev.platform_data;
	int ret = 0;
	int len = 0;

	if (!pdata) {
		pr_warn("%s: No platform data specified\n", __func__);
		return -EINVAL;
	}

	/* make sure at-least one of the GPIO is defined and that
	 * a name is specified for this instance */
	if (!pdata->name || (!gpio_is_valid(pdata->reset_gpio) &&
		!gpio_is_valid(pdata->shutdown_gpio))) {
		pr_warn("%s: invalid platform data\n", __func__);
		return -EINVAL;
	}

	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	if (pdata->gpio_runtime_setup) {
		ret = pdata->gpio_runtime_setup(pdev);
		if (ret) {
			pr_warn("%s: can't set up gpio\n", __func__);
			return ret;
		}
	}

	rfkill->pdata = pdata;

	len = strlen(pdata->name);
	rfkill->reset_name = kzalloc(len + 7, GFP_KERNEL);
	if (!rfkill->reset_name) {
		ret = -ENOMEM;
		goto fail_alloc;
	}

	rfkill->shutdown_name = kzalloc(len + 10, GFP_KERNEL);
	if (!rfkill->shutdown_name) {
		ret = -ENOMEM;
		goto fail_reset_name;
	}

	snprintf(rfkill->reset_name, len + 6 , "%s_reset", pdata->name);
	snprintf(rfkill->shutdown_name, len + 9, "%s_shutdown", pdata->name);

	if (pdata->power_clk_name) {
		rfkill->pwr_clk = clk_get(&pdev->dev, pdata->power_clk_name);
		if (IS_ERR(rfkill->pwr_clk)) {
			pr_warn("%s: can't find pwr_clk.\n", __func__);
			goto fail_shutdown_name;
		}
	}

	if (gpio_is_valid(pdata->reset_gpio)) {
		ret = gpio_request(pdata->reset_gpio, rfkill->reset_name);
		if (ret) {
			pr_warn("%s: failed to get reset gpio.\n", __func__);
			goto fail_clock;
		}
	}

	if (gpio_is_valid(pdata->shutdown_gpio)) {
		ret = gpio_request(pdata->shutdown_gpio, rfkill->shutdown_name);
		if (ret) {
			pr_warn("%s: failed to get shutdown gpio.\n", __func__);
			goto fail_reset;
		}
	}

	rfkill->rfkill_dev = rfkill_alloc(pdata->name, &pdev->dev, pdata->type,
				&rfkill_gpio_ops, rfkill);
	if (!rfkill->rfkill_dev)
		goto fail_shutdown;

	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto fail_rfkill;

	platform_set_drvdata(pdev, rfkill);

	dev_info(&pdev->dev, "%s device registered.\n", pdata->name);

	return 0;

fail_rfkill:
	rfkill_destroy(rfkill->rfkill_dev);
fail_shutdown:
	if (gpio_is_valid(pdata->shutdown_gpio))
		gpio_free(pdata->shutdown_gpio);
fail_reset:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
fail_clock:
	if (rfkill->pwr_clk)
		clk_put(rfkill->pwr_clk);
fail_shutdown_name:
	kfree(rfkill->shutdown_name);
fail_reset_name:
	kfree(rfkill->reset_name);
fail_alloc:
	kfree(rfkill);

	return ret;
}

static int rfkill_gpio_remove(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill = platform_get_drvdata(pdev);
	struct rfkill_gpio_platform_data *pdata = pdev->dev.platform_data;

	if (pdata->gpio_runtime_close)
		pdata->gpio_runtime_close(pdev);
	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);
	if (gpio_is_valid(rfkill->pdata->shutdown_gpio))
		gpio_free(rfkill->pdata->shutdown_gpio);
	if (gpio_is_valid(rfkill->pdata->reset_gpio))
		gpio_free(rfkill->pdata->reset_gpio);
	if (rfkill->pwr_clk && PWR_CLK_ENABLED(rfkill))
		clk_disable(rfkill->pwr_clk);
	if (rfkill->pwr_clk)
		clk_put(rfkill->pwr_clk);
	kfree(rfkill->shutdown_name);
	kfree(rfkill->reset_name);
	kfree(rfkill);

	return 0;
}

static struct platform_driver rfkill_gpio_driver = {
	.probe = rfkill_gpio_probe,
	.remove = __devexit_p(rfkill_gpio_remove),
	.driver = {
		   .name = "rfkill_gpio",
		   .owner = THIS_MODULE,
	},
};

static int __init rfkill_gpio_init(void)
{
	return platform_driver_register(&rfkill_gpio_driver);
}

static void __exit rfkill_gpio_exit(void)
{
	platform_driver_unregister(&rfkill_gpio_driver);
}

module_init(rfkill_gpio_init);
module_exit(rfkill_gpio_exit);

MODULE_DESCRIPTION("gpio rfkill");
MODULE_AUTHOR("NVIDIA");
MODULE_LICENSE("GPL");
