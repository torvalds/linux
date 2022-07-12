/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _MSM_SYSSTATS_H_
#define _MSM_SYSSTATS_H_

#include <uapi/linux/msm_sysstats.h>

#if IS_ENABLED(CONFIG_MSM_SYSSTATS)
extern void sysstats_register_kgsl_stats_cb(u64 (*cb)(pid_t pid));
extern void sysstats_unregister_kgsl_stats_cb(void);
#else
static inline void sysstats_register_kgsl_stats_cb(u64 (*cb)(pid_t pid))
{
}
static inline void sysstats_unregister_kgsl_stats_cb(void)
{
}
#endif
#endif /* _MSM_SYSSTATS_H_ */

