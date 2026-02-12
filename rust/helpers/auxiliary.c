// SPDX-License-Identifier: GPL-2.0

#include <linux/auxiliary_bus.h>

__rust_helper void
rust_helper_auxiliary_device_uninit(struct auxiliary_device *adev)
{
	return auxiliary_device_uninit(adev);
}

__rust_helper void
rust_helper_auxiliary_device_delete(struct auxiliary_device *adev)
{
	return auxiliary_device_delete(adev);
}
