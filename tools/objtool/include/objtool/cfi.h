/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _OBJTOOL_CFI_H
#define _OBJTOOL_CFI_H

#include <arch/cfi_regs.h>
#include <linux/list.h>

#define CFI_UNDEFINED		-1
#define CFI_CFA			-2
#define CFI_SP_INDIRECT		-3
#define CFI_BP_INDIRECT		-4

struct cfi_reg {
	int base;
	int offset;
};

struct cfi_init_state {
	struct cfi_reg regs[CFI_NUM_REGS];
	struct cfi_reg cfa;
};

struct cfi_state {
	struct hlist_node hash; /* must be first, cficmp() */
	struct cfi_reg regs[CFI_NUM_REGS];
	struct cfi_reg vals[CFI_NUM_REGS];
	struct cfi_reg cfa;
	int stack_size;
	int drap_reg, drap_offset;
	unsigned char type;
	bool bp_scratch;
	bool drap;
	bool end;
};

#endif /* _OBJTOOL_CFI_H */
