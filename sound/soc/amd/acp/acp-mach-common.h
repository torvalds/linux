/* SPDX-License-Identifier: GPL-2.0-only
 *  Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved
 */
/*
 * acp-mach-common.h - Structures shared across all AMD ACP machine drivers
 *                     (I2S, SoundWire, and SOF paths).
 */

#ifndef __ACP_MACH_COMMON_H
#define __ACP_MACH_COMMON_H

/**
 * struct amd_pdm_pdata - PDM controller platform data passed via mach->pdata
 * @pdm_sel: active PDM controller (ACP7X_PDM_DMIC0 or ACP7X_PDM_DMIC1),
 *           non-zero when a PDM controller was identified via ACPI _DSD for
 *           ACP7.B/7.F platforms.
 *
 * Passed via mach->pdata by machine select logic to both I2S and SoundWire
 * machine drivers so each can configure the correct DMIC DAI link.
 */
struct amd_pdm_pdata {
	unsigned int pdm_sel;
};

#endif /* __ACP_MACH_COMMON_H */
