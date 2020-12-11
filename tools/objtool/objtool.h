/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 Matt Helsley <mhelsley@vmware.com>
 */

#ifndef _OBJTOOL_H
#define _OBJTOOL_H

#include <stdbool.h>
#include <linux/list.h>
#include <linux/hashtable.h>

#include "elf.h"

#define __weak __attribute__((weak))

struct objtool_file {
	struct elf *elf;
	struct list_head insn_list;
	DECLARE_HASHTABLE(insn_hash, 20);
	struct list_head static_call_list;
	bool ignore_unreachables, c_file, hints, rodata;
};

struct objtool_file *objtool_open_read(const char *_objname);

int check(struct objtool_file *file);
int orc_dump(const char *objname);
int create_orc(struct objtool_file *file);
int create_orc_sections(struct objtool_file *file);

#endif /* _OBJTOOL_H */
