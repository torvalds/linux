/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilSECURE APIs.
 *
 *  Copyright (C) 2014-2022 Xilinx, Inc.
 *  Copyright (C) 2022-2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__
#define __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__

/**
 * struct xlnx_feature - Feature data
 * @family:	Family code of platform
 * @subfamily:	Subfamily code of platform
 * @feature_id:	Feature id of module
 * @data:	Collection of all supported platform data
 */
struct xlnx_feature {
	u32 family;
	u32 feature_id;
	void *data;
};

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_aes_engine(const u64 address, u32 *out);
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags);
void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map);
#else
static inline int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	return -ENODEV;
}

static inline int zynqmp_pm_sha_hash(const u64 address, const u32 size,
				     const u32 flags)
{
	return -ENODEV;
}

static inline void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif /* __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__ */
