// SPDX-License-Identifier: GPL-2.0

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "symbol.h"
#include "callchain.h"
#include "record.h"
#include "util/perf_regs.h"

void arch__add_leaf_frame_record_opts(struct record_opts *opts)
{
	opts->sample_user_regs |= sample_reg_masks[PERF_REG_ARM64_LR].mask;
}
