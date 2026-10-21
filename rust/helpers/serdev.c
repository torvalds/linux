// SPDX-License-Identifier: GPL-2.0

#include <linux/serdev.h>

__rust_helper
void rust_helper_serdev_device_driver_unregister(struct serdev_device_driver *sdrv)
{
	serdev_device_driver_unregister(sdrv);
}

__rust_helper
void rust_helper_serdev_device_put(struct serdev_device *serdev)
{
	serdev_device_put(serdev);
}

__rust_helper
void rust_helper_serdev_device_set_client_ops(struct serdev_device *serdev,
					      const struct serdev_device_ops *ops)
{
	serdev_device_set_client_ops(serdev, ops);
}
