/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#ifndef _UAPI_AMDXDNA_ACCEL_H_
#define _UAPI_AMDXDNA_ACCEL_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum amdxdna_device_type {
	AMDXDNA_DEV_TYPE_UNKNOWN = -1,
	AMDXDNA_DEV_TYPE_KMQ,
};

#if defined(__cplusplus)
} /* extern c end */
#endif

#endif /* _UAPI_AMDXDNA_ACCEL_H_ */
