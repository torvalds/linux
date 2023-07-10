/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SOC_QCOM_RPMH_H__
#define __SOC_QCOM_RPMH_H__

#include <soc/qcom/tcs.h>
#include <linux/platform_device.h>


#if IS_ENABLED(CONFIG_QCOM_RPMH)
int rpmh_write(const struct device *dev, enum rpmh_state state,
	       const struct tcs_cmd *cmd, u32 n);

int rpmh_write_async(const struct device *dev, enum rpmh_state state,
		     const struct tcs_cmd *cmd, u32 n);

int rpmh_write_batch(const struct device *dev, enum rpmh_state state,
		     const struct tcs_cmd *cmd, u32 *n);

int rpmh_mode_solver_set(const struct device *dev, bool enable);

int rpmh_write_sleep_and_wake(const struct device *dev);

int rpmh_write_sleep_and_wake_no_child(const struct device *dev);

void rpmh_invalidate(const struct device *dev);

int rpmh_init_fast_path(const struct device *dev,
			struct tcs_cmd *cmd, int n);

int rpmh_update_fast_path(const struct device *dev,
			  struct tcs_cmd *cmd, int n, u32 update_mask);
int rpmh_drv_start(const struct device *dev);

int rpmh_drv_stop(const struct device *dev);

const struct device *rpmh_get_device(const char *name, u32 drv_id);
#else

static inline int rpmh_write(const struct device *dev, enum rpmh_state state,
			     const struct tcs_cmd *cmd, u32 n)
{ return -ENODEV; }

static inline int rpmh_write_async(const struct device *dev,
				   enum rpmh_state state,
				   const struct tcs_cmd *cmd, u32 n)
{ return -ENODEV; }

static inline int rpmh_write_batch(const struct device *dev,
				   enum rpmh_state state,
				   const struct tcs_cmd *cmd, u32 *n)
{ return -ENODEV; }

static inline int rpmh_mode_solver_set(const struct device *dev, bool enable)
{ return -ENODEV; }

static inline int rpmh_write_sleep_and_wake(const struct device *dev)
{ return -ENODEV; }

static inline int rpmh_write_sleep_and_wake_no_child(const struct device *dev)
{ return -ENODEV; }

static inline void rpmh_invalidate(const struct device *dev)
{
}

static inline int rpmh_init_fast_path(const struct device *dev,
				      struct tcs_cmd *msg, int n)
{ return -ENODEV; }

static inline int rpmh_update_fast_path(const struct device *dev,
					struct tcs_cmd *msg, int n,
					u32 update_mask)
{ return -ENODEV; }

static inline int rpmh_drv_start(const struct device *dev)
{ return -ENODEV; }

static inline int rpmh_drv_stop(const struct device *dev)
{ return -ENODEV; }

static inline const struct device *rpmh_get_device(char *name, u32 drv_id)
{ return ERR_PTR(-ENODEV); }
#endif /* CONFIG_QCOM_RPMH */

#endif /* __SOC_QCOM_RPMH_H__ */
