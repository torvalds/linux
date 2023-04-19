/* SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB) OR BSD-2-Clause */
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#ifndef _PDS_COMMON_H_
#define _PDS_COMMON_H_

#define PDS_CORE_DRV_NAME			"pds_core"

/* the device's internal addressing uses up to 52 bits */
#define PDS_CORE_ADDR_LEN	52
#define PDS_CORE_ADDR_MASK	(BIT_ULL(PDS_ADDR_LEN) - 1)
#define PDS_PAGE_SIZE		4096

#endif /* _PDS_COMMON_H_ */
