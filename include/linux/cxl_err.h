/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 *
 * Author: Smita Koralahalli <Smita.KoralahalliChannabasappa@amd.com>
 */

#ifndef LINUX_CXL_ERR_H
#define LINUX_CXL_ERR_H

/* CXL RAS Capability Structure, CXL v3.1 sec 8.2.4.16 */
struct cxl_ras_capability_regs {
	u32 uncor_status;
	u32 uncor_mask;
	u32 uncor_severity;
	u32 cor_status;
	u32 cor_mask;
	u32 cap_control;
	u32 header_log[16];
};

#endif //__CXL_ERR_
