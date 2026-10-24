/* SPDX-License-Identifier: MIT */
/*
 * Function prototypes for timeout conversion helpers for wait ioctls.
 *
 * Copyright 2017 Red Hat
 * Copyright 2016 Advanced Micro Devices, Inc.
 */

#ifndef __DRM_TIMEOUT_H__
#define __DRM_TIMEOUT_H__

#include <linux/types.h>

signed long drm_timeout_abs_to_jiffies(s64 timeout_nsec);
unsigned long drm_timeout_rel_to_jiffies(u64 timeout_nsec);

#endif
