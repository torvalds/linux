/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_TEST_UTILS_H__
#define __BPF_TEST_UTILS_H__

#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* Clobber as many native registers and stack slots as possible. */
static __always_inline void clobber_regs_stack(void)
{
	char tmp_str[] = "123456789";
	unsigned long tmp;

	bpf_strtoul(tmp_str, sizeof(tmp_str), 0, &tmp);
	__sink(tmp);
}

#endif
