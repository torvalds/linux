/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2023 Intel Corporation. All rights reserved.
 *
 * telemetry data in debug windows
 */

#ifndef _SOF_INTEL_TELEMETRY_H
#define _SOF_INTEL_TELEMETRY_H

#include "../ipc4-telemetry.h"

struct xtensa_arch_block {
	u8	soc; /* should be equal to XTENSA_SOC_INTEL_ADSP */
	u16	version;
	u8	toolchain; /* ZEPHYR or XCC */

	u32	pc;
	u32	exccause;
	u32	excvaddr;
	u32	sar;
	u32	ps;
	u32	scompare1;
	u32	ar[XTENSA_CORE_AR_REGS_COUNT];
	u32	lbeg;
	u32	lend;
	u32	lcount;
} __packed;

void sof_ipc4_intel_dump_telemetry_state(struct snd_sof_dev *sdev, u32 flags);

#endif /* _SOF_INTEL_TELEMETRY_H */
