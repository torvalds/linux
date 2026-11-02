// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright (c) 2025 Qualcomm Technologies, Inc.

/*
 * WCD9378 (Tambora) SoundWire driver glue for the SDCA compute-mode
 * codec.  The SDCA topology and hardware-specific probe logic live in
 * wcd9378-sdca.c; this file carries the SoundWire slave driver plumbing
 * and the module boilerplate.
 */

#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "wcd9378-sdca.h"

static const struct dev_pm_ops wcd9378_sdw_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(wcd9378_sdca_system_suspend,
			    wcd9378_sdca_system_resume)
	RUNTIME_PM_OPS(wcd9378_sdca_runtime_suspend,
		       wcd9378_sdca_runtime_resume, NULL)
};

static const struct sdw_slave_ops wcd9378_sdw_ops = {
	.read_prop = wcd9378_sdca_read_prop,
};

static const struct sdw_device_id wcd9378_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x0217, 0x0110, 0),
	{ }
};
MODULE_DEVICE_TABLE(sdw, wcd9378_sdw_id);

static struct sdw_driver wcd9378_sdw_driver = {
	.driver = {
		.name	= "wcd9378",
		.pm	= pm_ptr(&wcd9378_sdw_pm_ops),
	},
	.probe		= wcd9378_sdca_probe,
	.remove		= wcd9378_sdca_remove,
	.id_table	= wcd9378_sdw_id,
	.ops		= &wcd9378_sdw_ops,
};
module_sdw_driver(wcd9378_sdw_driver);

MODULE_DESCRIPTION("Qualcomm WCD9378 (Tambora) SoundWire codec");
MODULE_AUTHOR("Qualcomm Technologies, Inc.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS("SND_SOC_SDCA");
MODULE_IMPORT_NS("SND_SOC_SDCA_CLASS");
