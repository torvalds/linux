/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2019 Intel Corporation. All rights reserved.
 *
 * Author: Keyon Jie <yang.jie@linux.intel.com>
 */

#ifndef __SOF_INTEL_HDA_IPC_H
#define __SOF_INTEL_HDA_IPC_H

/*
 * Primary register, mapped to
 * - DIPCTDR (HIPCIDR) in sideband IPC (cAVS 1.8+)
 * - DIPCT in cAVS 1.5 IPC
 *
 * Secondary register, mapped to:
 * - DIPCTDD (HIPCIDD) in sideband IPC (cAVS 1.8+)
 * - DIPCTE in cAVS 1.5 IPC
 */

/* Common bits in primary register */

/* Reserved for doorbell */
#define HDA_IPC_RSVD_31		BIT(31)
/* Target, 0 - normal message, 1 - compact message(cAVS compatible) */
#define HDA_IPC_MSG_COMPACT	BIT(30)
/* Direction, 0 - request, 1 - response */
#define HDA_IPC_RSP		BIT(29)

#define HDA_IPC_TYPE_SHIFT	24
#define HDA_IPC_TYPE_MASK	GENMASK(28, 24)
#define HDA_IPC_TYPE(x)		((x) << HDA_IPC_TYPE_SHIFT)

#define HDA_IPC_PM_GATE		HDA_IPC_TYPE(0x8U)

/* Command specific payload bits in secondary register */

/* Disable DMA tracing (0 - keep tracing, 1 - to disable DMA trace) */
#define HDA_PM_NO_DMA_TRACE	BIT(4)
/* Prevent clock gating (0 - cg allowed, 1 - DSP clock always on) */
#define HDA_PM_PCG		BIT(3)
/* Prevent power gating (0 - deep power state transitions allowed) */
#define HDA_PM_PPG		BIT(2)
/* Indicates whether streaming is active */
#define HDA_PM_PG_STREAMING	BIT(1)
#define HDA_PM_PG_RSVD		BIT(0)

#endif
