// SPDX-License-Identifier: GPL-2.0
#include <errno.h>

#ifndef REMOTE_UNWIND_LIBUNWIND
#include <libunwind.h>
#include "perf_regs.h"
#include "../../../util/unwind.h"
#endif
#include "../../../util/debug.h"

int LIBUNWIND__ARCH_REG_ID(int regnum)
{
	if (regnum < 0 || regnum >= PERF_REG_ARM64_EXTENDED_MAX)
		return -EINVAL;

	return regnum;
}
