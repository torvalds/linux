/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2024 Linaro Ltd.
 */

#ifndef __EXYNOS_ACPM_PROTOCOL_H
#define __EXYNOS_ACPM_PROTOCOL_H

#include <linux/types.h>

struct acpm_handle;
struct device_node;

struct acpm_dvfs_ops {
	int (*set_rate)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			unsigned int clk_id, unsigned long rate);
	unsigned long (*get_rate)(struct acpm_handle *handle,
				  unsigned int acpm_chan_id,
				  unsigned int clk_id);
};

struct acpm_pmic_ops {
	int (*read_reg)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			u8 type, u8 reg, u8 chan, u8 *buf);
	int (*bulk_read)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			 u8 type, u8 reg, u8 chan, u8 count, u8 *buf);
	int (*write_reg)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			 u8 type, u8 reg, u8 chan, u8 value);
	int (*bulk_write)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			  u8 type, u8 reg, u8 chan, u8 count, const u8 *buf);
	int (*update_reg)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			  u8 type, u8 reg, u8 chan, u8 value, u8 mask);
};

struct acpm_tmu_ops {
	int (*init)(struct acpm_handle *handle, unsigned int acpm_chan_id);
	int (*read_temp)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			 u8 tz, int *temp);
	int (*set_threshold)(struct acpm_handle *handle,
			     unsigned int acpm_chan_id, u8 tz,
			     const u8 temperature[8], size_t tlen);
	int (*set_interrupt_enable)(struct acpm_handle *handle,
				    unsigned int acpm_chan_id, u8 tz, u8 inten);
	int (*tz_control)(struct acpm_handle *handle, unsigned int acpm_chan_id,
			  u8 tz, bool enable);
	int (*clear_tz_irq)(struct acpm_handle *handle,
			    unsigned int acpm_chan_id, u8 tz);
	int (*suspend)(struct acpm_handle *handle, unsigned int acpm_chan_id);
	int (*resume)(struct acpm_handle *handle, unsigned int acpm_chan_id);
};

struct acpm_ops {
	struct acpm_dvfs_ops dvfs;
	struct acpm_pmic_ops pmic;
	struct acpm_tmu_ops tmu;
};

/**
 * struct acpm_handle - Reference to an initialized protocol instance
 * @ops:	pointer to the constant ACPM protocol operations.
 */
struct acpm_handle {
	const struct acpm_ops *ops;
};

struct device;

struct acpm_handle *devm_acpm_get_by_node(struct device *dev,
					  struct device_node *np);
struct acpm_handle *devm_acpm_get_by_phandle(struct device *dev);

#endif /* __EXYNOS_ACPM_PROTOCOL_H */
