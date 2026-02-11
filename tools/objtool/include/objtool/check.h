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
	struct instruction *first_insn, *last_insn, *nop;

	/*
	 * Byte-offset-addressed len-sized array of pointers to CFI structs.
	 * This is shared with the other alt_groups in the same alternative.
	 */
	struct cfi_state **cfi;

	bool ignore;
	unsigned int feature;
};

enum alternative_type {
	ALT_TYPE_INSTRUCTIONS,
	ALT_TYPE_JUMP_TABLE,
	ALT_TYPE_EX_TABLE,
};

struct alternative {
	struct alternative *next;
	struct instruction *insn;
	enum alternative_type type;
};

#define INSN_CHUNK_BITS		8
#define INSN_CHUNK_SIZE		(1 << INSN_CHUNK_BITS)
#define INSN_CHUNK_MAX		(INSN_CHUNK_SIZE - 1)

struct instruction {
	struct hlist_node hash;
	struct list_head call_node;
	struct section *sec;
	unsigned long offset;
	unsigned long immediate;

	u8 len;
	u8 prev_len;
	u8 type;
	s8 instr;

	u32 idx			: INSN_CHUNK_BITS,
	    dead_end		: 1,
	    ignore_alts		: 1,
	    hint		: 1,
	    save		: 1,
	    restore		: 1,
	    retpoline_safe	: 1,
	    noendbr		: 1,
	    unret		: 1,
	    visited		: 4,
	    no_reloc		: 1,
	    hole		: 1,
	    fake		: 1,
	    trace		: 1;
		/* 9 bit hole */

	struct alt_group *alt_group;
	struct instruction *jump_dest;
	struct instruction *first_jump_src;
	union {
		struct symbol *_call_dest;
		struct {
			struct reloc *_jump_table;
			unsigned long _jump_table_size;
		};
	};
	struct alternative *alts;
	struct symbol *sym;
	struct stack_op *stack_ops;
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
#define VISITED_UNRET		0x04

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

static inline struct symbol *insn_call_dest(struct instruction *insn)
{
	if (insn->type == INSN_JUMP_DYNAMIC ||
	    insn->type == INSN_CALL_DYNAMIC)
		return NULL;

	return insn->_call_dest;
}

struct instruction *find_insn(struct objtool_file *file,
			      struct section *sec, unsigned long offset);

struct instruction *next_insn_same_sec(struct objtool_file *file, struct instruction *insn);

#define sec_for_each_insn(file, _sec, insn)				\
	for (insn = find_insn(file, _sec, 0);				\
	     insn && insn->sec == _sec;					\
	     insn = next_insn_same_sec(file, insn))

#define sym_for_each_insn(file, sym, insn)				\
	for (insn = find_insn(file, sym->sec, sym->offset);		\
	     insn && insn->offset < sym->offset + sym->len;		\
	     insn = next_insn_same_sec(file, insn))

const char *objtool_disas_insn(struct instruction *insn);

extern size_t sym_name_max_len;
extern struct disas_context *objtool_disas_ctx;
int pv_ops_idx_off(const char *symname);

#endif /* _CHECK_H */
