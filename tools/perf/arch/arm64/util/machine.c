// SPDX-License-Identifier: GPL-2.0

#include "callchain.h" // prototype of arch__add_leaf_frame_record_opts
#include "perf_regs.h"
#include "record.h"

#define SMPL_REG_MASK(b) (1ULL << (b))

void arch__add_leaf_frame_record_opts(struct record_opts *opts)
{
	opts->sample_user_regs |= SMPL_REG_MASK(PERF_REG_ARM64_LR);
}
