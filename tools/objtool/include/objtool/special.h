/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _SPECIAL_H
#define _SPECIAL_H

#include <stdbool.h>
#include <objtool/check.h>
#include <objtool/elf.h>

#define C_JUMP_TABLE_SECTION ".rodata..c_jump_table"

struct special_alt {
	struct list_head list;

	bool group;
	bool skip_orig;
	bool skip_alt;
	bool jump_or_nop;
	u8 key_addend;

	struct section *orig_sec;
	unsigned long orig_off;

	struct section *new_sec;
	unsigned long new_off;

	unsigned int orig_len, new_len; /* group only */
};

int special_get_alts(struct elf *elf, struct list_head *alts);

void arch_handle_alternative(unsigned short feature, struct special_alt *alt);

bool arch_support_alt_relocation(struct special_alt *special_alt,
				 struct instruction *insn,
				 struct reloc *reloc);
struct reloc *arch_find_switch_table(struct objtool_file *file,
				     struct instruction *insn,
				     unsigned long *table_size);
#endif /* _SPECIAL_H */
