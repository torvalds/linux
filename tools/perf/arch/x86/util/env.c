// SPDX-License-Identifier: GPL-2.0
#include "linux/string.h"
#include "util/env.h"
#include "env.h"

bool x86__is_amd_cpu(void)
{
	struct perf_env env = { .total_mem = 0, };
	static int is_amd; /* 0: Uninitialized, 1: Yes, -1: No */

	if (is_amd)
		goto ret;

	perf_env__cpuid(&env);
	is_amd = env.cpuid && strstarts(env.cpuid, "AuthenticAMD") ? 1 : -1;
	perf_env__exit(&env);
ret:
	return is_amd >= 1 ? true : false;
}
