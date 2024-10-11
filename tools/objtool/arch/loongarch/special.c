// SPDX-License-Identifier: GPL-2.0-or-later
#include <objtool/special.h>

bool arch_support_alt_relocation(struct special_alt *special_alt,
				 struct instruction *insn,
				 struct reloc *reloc)
{
	return false;
}

struct reloc *arch_find_switch_table(struct objtool_file *file,
				     struct instruction *insn,
				     unsigned long *table_size)
{
	return NULL;
}
