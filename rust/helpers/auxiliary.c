// SPDX-License-Identifier: GPL-2.0

#include <linux/auxiliary_bus.h>

void rust_helper_auxiliary_set_drvdata(struct auxiliary_device *adev, void *data)
{
	auxiliary_set_drvdata(adev, data);
}

void *rust_helper_auxiliary_get_drvdata(struct auxiliary_device *adev)
{
	return auxiliary_get_drvdata(adev);
}

void rust_helper_auxiliary_device_uninit(struct auxiliary_device *adev)
{
	return auxiliary_device_uninit(adev);
}

void rust_helper_auxiliary_device_delete(struct auxiliary_device *adev)
{
	return auxiliary_device_delete(adev);
}
