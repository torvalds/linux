// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>

__rust_helper int rust_helper_devm_add_action(struct device *dev,
					      void (*action)(void *),
					      void *data)
{
	return devm_add_action(dev, action, data);
}

__rust_helper int rust_helper_devm_add_action_or_reset(struct device *dev,
						       void (*action)(void *),
						       void *data)
{
	return devm_add_action_or_reset(dev, action, data);
}

__rust_helper void *rust_helper_dev_get_drvdata(const struct device *dev)
{
	return dev_get_drvdata(dev);
}

__rust_helper void rust_helper_dev_set_drvdata(struct device *dev, void *data)
{
	dev_set_drvdata(dev, data);
}
