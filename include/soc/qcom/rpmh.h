/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#ifndef __SOC_QCOM_RPMH_H__
#define __SOC_QCOM_RPMH_H__

#include <soc/qcom/tcs.h>
#include <linux/platform_device.h>


#if IS_ENABLED(CONFIG_QCOM_RPMH)
int rpmh_write(const struct device *dev, enum rpmh_state state,
	       const struct tcs_cmd *cmd, u32 n);

int rpmh_flush(const struct device *dev);

int rpmh_invalidate(const struct device *dev);

#else

static inline int rpmh_write(const struct device *dev, enum rpmh_state state,
			     const struct tcs_cmd *cmd, u32 n)
{ return -ENODEV; }


static inline int rpmh_flush(const struct device *dev)
{ return -ENODEV; }

static inline int rpmh_invalidate(const struct device *dev)
{ return -ENODEV; }

#endif /* CONFIG_QCOM_RPMH */

#endif /* __SOC_QCOM_RPMH_H__ */
