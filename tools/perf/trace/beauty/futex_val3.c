// SPDX-License-Identifier: LGPL-2.1
#include <linux/futex.h>

#ifndef FUTEX_BITSET_MATCH_ANY
#define FUTEX_BITSET_MATCH_ANY 0xffffffff
#endif

static size_t syscall_arg__scnprintf_futex_val3(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned int bitset = arg->val;

	if (bitset == FUTEX_BITSET_MATCH_ANY)
		return scnprintf(bf, size, "MATCH_ANY");

	return scnprintf(bf, size, "%#xd", bitset);
}

#define SCA_FUTEX_VAL3  syscall_arg__scnprintf_futex_val3
