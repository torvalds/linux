/*
 * ARM Kernel Development
 * 
 * simple_driver.c - Simple platform driver to demonstrate device
 * probing
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of_device.h>

MODULE_DESCRIPTION("Simple driver");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

static const struct of_device_id simple_device_ids[] = {
	/* TODO 2/2: Add compatible strings */
	{ .compatible = "so2,simple-device-v1"},
	{ .compatible = "so2,simple-device-v2"},
	{ /* sentinel */}
};

static int simple_probe(struct platform_device *pdev)
{
	pr_info("simple_probe() %pOF\n", pdev->dev.of_node);

	return 0;
}

static int simple_remove(struct platform_device *pdev)
{
	pr_info("simple_remove()\n");

	return 0;
}

struct platform_driver simple_driver = {
	.probe	= simple_probe,
	.remove	= simple_remove,
	.driver = {
		.name = "simple_driver",
		.of_match_table = simple_device_ids,
	},
};

static int simple_init(void)
{
	pr_info("Simple driver init!\n");
	
	/* TODO 1/0: Notice simple_driver definition */
	return platform_driver_register(&simple_driver);
}

static void simple_exit(void)
{
	pr_info("Simple driver exit\n");

	platform_driver_unregister(&simple_driver);
}

module_init(simple_init);
module_exit(simple_exit);
