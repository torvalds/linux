/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _CHECK_H
#define _CHECK_H

#include <stdbool.h>
#include <objtool/cfi.h>
#include <objtool/arch.h>

struct insn_state {
	struct cfi_state cfi;
	unsigned int uaccess_stack;
	bool uaccess;
	bool df;
	bool noinstr;
	s8 instr;
};

struct alt_group {
	/*
	 * Pointer from a replacement group to the original group.  NULL if it
	 * *is* the original group.
	 */
	struct alt_group *orig_group;

	/* First and last instructions in the group */
	struct instruction *first_insn, *last_insn;

	/*
	 * Byte-offset-addressed len-sized array of pointers to CFI structs.
	 * This is shared with the other alt_groups in the same alternative.
	 */
	struct cfi_state **cfi;
};

struct instruction {
	struct list_head list;
	struct hlist_node hash;
	struct list_head call_node;
	struct section *sec;
	unsigned long offset;
	unsigned int len;
	enum insn_type type;
	unsigned long immediate;

	u16 dead_end		: 1,
	   ignore		: 1,
	   ignore_alts		: 1,
	   hint			: 1,
	   save			: 1,
	   restore		: 1,
	   retpoline_safe	: 1,
	   noendbr		: 1,
	   entry		: 1;
		/* 7 bit hole */

	s8 instr;
	u8 visited;

	struct alt_group *alt_group;
	struct symbol *call_dest;
	struct instruction *jump_dest;
	struct instruction *first_jump_src;
	struct reloc *jump_table;
	struct reloc *reloc;
	struct list_head alts;
	struct symbol *sym;
	struct list_head stack_ops;
	struct cfi_state *cfi;
};

static inline struct symbol *insn_func(struct instruction *insn)
{
	struct symbol *sym = insn->sym;

	if (sym && sym->type != STT_FUNC)
		sym = NULL;

	return sym;
}

#define VISITED_BRANCH		0x01
#define VISITED_BRANCH_UACCESS	0x02
#define VISITED_BRANCH_MASK	0x03
#define VISITED_ENTRY		0x04

static inline bool is_static_jump(struct instruction *insn)
{
	return insn->type == INSN_JUMP_CONDITIONAL ||
	       insn->type == INSN_JUMP_UNCONDITIONAL;
}

static inline bool is_dynamic_jump(struct instruction *insn)
{
	return insn->type == INSN_JUMP_DYNAMIC ||
	       insn->type == INSN_JUMP_DYNAMIC_CONDITIONAL;
}

static inline bool is_jump(struct instruction *insn)
{
	return is_static_jump(insn) || is_dynamic_jump(insn);
}

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
