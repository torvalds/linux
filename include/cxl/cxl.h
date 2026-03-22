/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 Advanced Micro Devices, Inc. */
#ifndef _CXL_H_
#define _CXL_H_

#ifdef CONFIG_CXL_REGION
bool cxl_region_contains_resource(struct resource *res);
#else
static inline bool cxl_region_contains_resource(struct resource *res)
{
	return false;
}
#endif

#endif /* _CXL_H_ */
