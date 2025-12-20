/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilSECURE APIs.
 *
 *  Copyright (C) 2014-2022 Xilinx, Inc.
 *  Copyright (C) 2022-2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__
#define __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_aes_engine(const u64 address, u32 *out);
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags);
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
#endif

#endif /* __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__ */
