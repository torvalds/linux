/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Vijendar Mukunda <Vijendar.Mukunda@amd.com>
 */

#ifndef __SOF_AMD_ACP7X_H
#define __SOF_AMD_ACP7X_H

/* Return values for sof_amd_check_and_handle_acp7x_sdw_wake_irq() */
#define WAKE_IRQ_HANDLED			1
#define WAKE_IRQ_NONE				0

/* ACP7X SoundWire IO structures */

struct sof_amd_acp7x_sdw_err_regs {
	u32 err_stat_mask;
	u32 fifo_err_reason;
	u32 err_reason1;
	u32 err_reason2;
};

struct acp7x_sdw_wake_src {
	u32 host_stat_mask;
	u32 pme_sts_reg;
	u32 wake_en_mask;
	u8  instance;
};

extern const struct sof_amd_acp7x_sdw_err_regs acp7x_sdw_err_regs[ACP7X_SDW_MAX_MANAGER_COUNT];
extern const struct acp7x_sdw_wake_src acp7x_sdw_wake_sources[];

#endif /* __SOF_AMD_ACP7X_H */
