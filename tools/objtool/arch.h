/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _ARCH_H
#define _ARCH_H

#include <stdbool.h>
#include <linux/list.h>
#include "elf.h"
#include "cfi.h"

#define INSN_JUMP_CONDITIONAL	1
#define INSN_JUMP_UNCONDITIONAL	2
#define INSN_JUMP_DYNAMIC	3
#define INSN_CALL		4
#define INSN_CALL_DYNAMIC	5
#define INSN_RETURN		6
#define INSN_CONTEXT_SWITCH	7
#define INSN_STACK		8
#define INSN_BUG		9
#define INSN_NOP		10
#define INSN_STAC		11
#define INSN_CLAC		12
#define INSN_STD		13
#define INSN_CLD		14
#define INSN_OTHER		15
#define INSN_LAST		INSN_OTHER

enum op_dest_type {
	OP_DEST_REG,
	OP_DEST_REG_INDIRECT,
	OP_DEST_MEM,
	OP_DEST_PUSH,
	OP_DEST_PUSHF,
	OP_DEST_LEAVE,
};

struct op_dest {
	enum op_dest_type type;
	unsigned char reg;
	int offset;
};

enum op_src_type {
	OP_SRC_REG,
	OP_SRC_REG_INDIRECT,
	OP_SRC_CONST,
	OP_SRC_POP,
	OP_SRC_POPF,
	OP_SRC_ADD,
	OP_SRC_AND,
};

struct op_src {
	enum op_src_type type;
	unsigned char reg;
	int offset;
};

struct stack_op {
	struct op_dest dest;
	struct op_src src;
};

void arch_initial_func_cfi_state(struct cfi_state *state);

int arch_decode_instruction(struct elf *elf, struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    unsigned int *len, unsigned char *type,
			    unsigned long *immediate, struct stack_op *op);

bool arch_callee_saved_reg(unsigned char reg);

#endif /* _ARCH_H */
