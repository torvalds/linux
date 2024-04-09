/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OBJTOOL_ORC_H
#define _OBJTOOL_ORC_H

#include <objtool/check.h>

int init_orc_entry(struct orc_entry *orc, struct cfi_state *cfi, struct instruction *insn);
void orc_print_dump(struct elf *dummy_elf, struct orc_entry *orc, int i);
int write_orc_entry(struct elf *elf, struct section *orc_sec,
		    struct section *ip_sec, unsigned int idx,
		    struct section *insn_sec, unsigned long insn_off,
		    struct orc_entry *o);

#endif /* _OBJTOOL_ORC_H */
