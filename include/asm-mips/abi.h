/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005, 06 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2005 MIPS Technologies, Inc.
 */
#ifndef _ASM_ABI_H
#define _ASM_ABI_H

#include <asm/signal.h>
#include <asm/siginfo.h>

struct mips_abi {
	void (* const do_signal)(struct pt_regs *regs);
	int (* const setup_frame)(struct k_sigaction * ka,
	                          struct pt_regs *regs, int signr,
	                          sigset_t *set);
	int (* const setup_rt_frame)(struct k_sigaction * ka,
	                       struct pt_regs *regs, int signr,
	                       sigset_t *set, siginfo_t *info);
};

#endif /* _ASM_ABI_H */
