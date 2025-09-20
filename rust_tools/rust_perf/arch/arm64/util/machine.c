// SPDX-License-Identifier: GPL-2.0

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "symbol.h"
#include "callchain.h"
#include "perf_regs.h"
#include "record.h"
#include "util/perf_regs.h"

void arch__add_leaf_frame_record_opts(struct record_opts *opts)
{
	const struct sample_reg *sample_reg_masks = arch__sample_reg_masks();

	opts->sample_user_regs |= sample_reg_masks[PERF_REG_ARM64_LR].mask;
}
