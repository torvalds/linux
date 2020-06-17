/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _CHECK_H
#define _CHECK_H

#include <stdbool.h>
#include "cfi.h"
#include "arch.h"

struct insn_state {
	struct cfi_state cfi;
	unsigned int uaccess_stack;
	bool uaccess;
	bool df;
	bool noinstr;
	s8 instr;
};

struct instruction {
	struct list_head list;
	struct hlist_node hash;
	struct section *sec;
	unsigned long offset;
	unsigned int len;
	enum insn_type type;
	unsigned long immediate;
	bool dead_end, ignore, ignore_alts;
	bool hint;
	bool retpoline_safe;
	s8 instr;
	u8 visited;
	u8 ret_offset;
	int alt_group;
	struct symbol *call_dest;
	struct instruction *jump_dest;
	struct instruction *first_jump_src;
	struct rela *jump_table;
	struct list_head alts;
	struct symbol *func;
	struct list_head stack_ops;
	struct cfi_state cfi;
	struct orc_entry orc;
};

struct instruction *find_insn(struct objtool_file *file,
			      struct section *sec, unsigned long offset);

#define for_each_insn(file, insn)					\
	list_for_each_entry(insn, &file->insn_list, list)

#define sec_for_each_insn(file, sec, insn)				\
	for (insn = find_insn(file, sec, 0);				\
	     insn && &insn->list != &file->insn_list &&			\
			insn->sec == sec;				\
	     insn = list_next_entry(insn, list))


#endif /* _CHECK_H */
