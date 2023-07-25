/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_DCVS_H
#define _QCOM_DCVS_H

#include <linux/kernel.h>

#define QCOM_DCVS_WIDTH_PROP	"qcom,bus-width"
#define QCOM_DCVS_HW_PROP	"qcom,dcvs-hw-type"
#define QCOM_DCVS_PATH_PROP	"qcom,dcvs-path-type"

enum dcvs_hw_type {
	DCVS_DDR,
	DCVS_LLCC,
	DCVS_L3,
	DCVS_DDRQOS,
	DCVS_UBWCP,
	DCVS_L3_1,
	NUM_DCVS_HW_TYPES
};

enum dcvs_path_type {
	DCVS_SLOW_PATH,
	DCVS_FAST_PATH,
	DCVS_PERCPU_PATH,
	NUM_DCVS_PATHS
};

struct dcvs_freq {
	u32			ib;
	u32			ab;
	enum dcvs_hw_type	hw_type;
};

#if IS_ENABLED(CONFIG_QCOM_DCVS)
int qcom_dcvs_register_voter(const char *name, enum dcvs_hw_type hw_type,
			enum dcvs_path_type path_type);
int qcom_dcvs_unregister_voter(const char *name, enum dcvs_hw_type hw_type,
			enum dcvs_path_type path_type);
int qcom_dcvs_update_votes(const char *name, struct dcvs_freq *votes,
			u32 update_mask, enum dcvs_path_type path);
struct kobject *qcom_dcvs_kobject_get(enum dcvs_hw_type type);
int qcom_dcvs_hw_minmax_get(enum dcvs_hw_type hw_type, u32 *min, u32 *max);
struct device_node *qcom_dcvs_get_ddr_child_node(struct device_node *of_parent);
#else
static inline int qcom_dcvs_register_voter(const char *name,
		enum dcvs_hw_type hw_type, enum dcvs_path_type path_type)
{
	return -ENODEV;
}
static inline int qcom_dcvs_unregister_voter(const char *name,
		enum dcvs_hw_type hw_type, enum dcvs_path_type path_type)
{
	return -ENODEV;
}
static inline int qcom_dcvs_update_votes(const char *name,
		struct dcvs_freq *votes, u32 update_mask,
		enum dcvs_path_type path)
{
	return -ENODEV;
}
static inline struct kobject *qcom_dcvs_kobject_get(enum dcvs_hw_type type)
{
	return NULL;
}
static inline int qcom_dcvs_hw_minmax_get(enum dcvs_hw_type hw_type, u32 *min,
		u32 *max)
{
	return -ENODEV;
}
static inline struct device_node *qcom_dcvs_get_ddr_child_node(
					struct device_node *of_parent)
{
	return NULL;
}
#endif

#endif /* _QCOM_DCVS_H */
