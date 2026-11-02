/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/* Copyright (c) 2025 Qualcomm Technologies, Inc. */

#ifndef _WCD9378_SDCA_H
#define _WCD9378_SDCA_H

#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>

int  wcd9378_sdca_probe(struct sdw_slave *slave,
			const struct sdw_device_id *id);
void wcd9378_sdca_remove(struct sdw_slave *slave);
int  wcd9378_sdca_read_prop(struct sdw_slave *slave);

int wcd9378_sdca_runtime_suspend(struct device *dev);
int wcd9378_sdca_runtime_resume(struct device *dev);
int wcd9378_sdca_system_suspend(struct device *dev);
int wcd9378_sdca_system_resume(struct device *dev);

#endif /* _WCD9378_SDCA_H */
