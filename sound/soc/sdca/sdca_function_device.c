// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2024 Intel Corporation.

/*
 * SDCA Function Device management
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/auxiliary_bus.h>
#include <linux/soundwire/sdw.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include "sdca_function_device.h"

/*
 * A SoundWire device can have multiple SDCA functions identified by
 * their type and ADR. there can be multiple SoundWire devices per
 * link, or multiple devices spread across multiple links. An IDA is
 * required to identify each instance.
 */
static DEFINE_IDA(sdca_function_ida);

static void sdca_dev_release(struct device *dev)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct sdca_dev *sdev = auxiliary_dev_to_sdca_dev(auxdev);

	ida_free(&sdca_function_ida, auxdev->id);
	kfree(sdev);
}

/* alloc, init and add link devices */
static struct sdca_dev *sdca_dev_register(struct device *parent,
					  struct sdca_function_desc *function_desc)
{
	struct sdca_dev *sdev;
	struct auxiliary_device *auxdev;
	int ret;
	int rc;

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return ERR_PTR(-ENOMEM);

	auxdev = &sdev->auxdev;
	auxdev->name = function_desc->name;
	auxdev->dev.parent = parent;
	auxdev->dev.fwnode = function_desc->node;
	auxdev->dev.release = sdca_dev_release;

	sdev->function.desc = function_desc;

	rc = ida_alloc(&sdca_function_ida, GFP_KERNEL);
	if (rc < 0) {
		kfree(sdev);
		return ERR_PTR(rc);
	}
	auxdev->id = rc;

	/* now follow the two-step init/add sequence */
	ret = auxiliary_device_init(auxdev);
	if (ret < 0) {
		dev_err(parent, "failed to initialize SDCA function dev %s\n",
			function_desc->name);
		ida_free(&sdca_function_ida, auxdev->id);
		kfree(sdev);
		return ERR_PTR(ret);
	}

	ret = auxiliary_device_add(auxdev);
	if (ret < 0) {
		dev_err(parent, "failed to add SDCA function dev %s\n",
			sdev->auxdev.name);
		/* sdev will be freed with the put_device() and .release sequence */
		auxiliary_device_uninit(&sdev->auxdev);
		return ERR_PTR(ret);
	}

	return sdev;
}

static void sdca_dev_unregister(struct sdca_dev *sdev)
{
	auxiliary_device_delete(&sdev->auxdev);
	auxiliary_device_uninit(&sdev->auxdev);
}

int sdca_dev_register_functions(struct sdw_slave *slave)
{
	struct sdca_device_data *sdca_data = &slave->sdca_data;
	int i;

	for (i = 0; i < sdca_data->num_functions; i++) {
		struct sdca_dev *func_dev;

		func_dev = sdca_dev_register(&slave->dev,
					     &sdca_data->function[i]);
		if (IS_ERR(func_dev))
			return PTR_ERR(func_dev);

		sdca_data->function[i].func_dev = func_dev;
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_dev_register_functions, "SND_SOC_SDCA");

void sdca_dev_unregister_functions(struct sdw_slave *slave)
{
	struct sdca_device_data *sdca_data = &slave->sdca_data;
	int i;

	for (i = 0; i < sdca_data->num_functions; i++)
		sdca_dev_unregister(sdca_data->function[i].func_dev);
}
EXPORT_SYMBOL_NS(sdca_dev_unregister_functions, "SND_SOC_SDCA");
