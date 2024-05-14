/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ARM_FRAME_POINTER_UNWIND_SUPPORT_H
#define __PERF_ARM_FRAME_POINTER_UNWIND_SUPPORT_H

#include "event.h"
#include "thread.h"

u64 get_leaf_frame_caller_aarch64(struct perf_sample *sample, struct thread *thread, int user_idx);

#endif /* __PERF_ARM_FRAME_POINTER_UNWIND_SUPPORT_H */
