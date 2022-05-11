/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#ifndef __SOUND_SOC_SOF_IPC4_PRIV_H
#define __SOUND_SOC_SOF_IPC4_PRIV_H

#include <linux/idr.h>
#include "sof-priv.h"

/**
 * struct sof_ipc4_fw_data - IPC4-specific data
 * @manifest_fw_hdr_offset: FW header offset in the manifest
 * @num_fw_modules : Number of modules in base FW
 * @fw_modules: Array of base FW modules
 */
struct sof_ipc4_fw_data {
	u32 manifest_fw_hdr_offset;
	int num_fw_modules;
	void *fw_modules;
};

#endif
