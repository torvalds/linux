// SPDX-License-Identifier: GPL-2.0
#include "arm64-frame-pointer-unwind-support.h"
#include "callchain.h"
#include "event.h"
#include "perf_regs.h" // SMPL_REG_MASK
#include "unwind.h"

#define perf_event_arm_regs perf_event_arm64_regs
#include "../../arch/arm64/include/uapi/asm/perf_regs.h"
#undef perf_event_arm_regs

struct entries {
	u64 stack[2];
	size_t length;
};

static bool get_leaf_frame_caller_enabled(struct perf_sample *sample)
{
	return callchain_param.record_mode == CALLCHAIN_FP && sample->user_regs.regs
		&& sample->user_regs.mask & SMPL_REG_MASK(PERF_REG_ARM64_LR);
}

static int add_entry(struct unwind_entry *entry, void *arg)
{
	struct entries *entries = arg;

	entries->stack[entries->length++] = entry->ip;
	return 0;
}

u64 get_leaf_frame_caller_aarch64(struct perf_sample *sample, struct thread *thread, int usr_idx)
{
	int ret;
	struct entries entries = {};
	struct regs_dump old_regs = sample->user_regs;

	if (!get_leaf_frame_caller_enabled(sample))
		return 0;

	/*
	 * If PC and SP are not recorded, get the value of PC from the stack
	 * and set its mask. SP is not used when doing the unwinding but it
	 * still needs to be set to prevent failures.
	 */

	if (!(sample->user_regs.mask & SMPL_REG_MASK(PERF_REG_ARM64_PC))) {
		sample->user_regs.cache_mask |= SMPL_REG_MASK(PERF_REG_ARM64_PC);
		sample->user_regs.cache_regs[PERF_REG_ARM64_PC] = sample->callchain->ips[usr_idx+1];
	}

	if (!(sample->user_regs.mask & SMPL_REG_MASK(PERF_REG_ARM64_SP))) {
		sample->user_regs.cache_mask |= SMPL_REG_MASK(PERF_REG_ARM64_SP);
		sample->user_regs.cache_regs[PERF_REG_ARM64_SP] = 0;
	}

	ret = unwind__get_entries(add_entry, &entries, thread, sample, 2);
	sample->user_regs = old_regs;

	if (ret || entries.length != 2)
		return ret;

	return callchain_param.order == ORDER_CALLER ? entries.stack[0] : entries.stack[1];
}
