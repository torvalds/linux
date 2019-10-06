/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _ORC_H
#define _ORC_H

#include <asm/orc_types.h>

struct objtool_file;

int create_orc(struct objtool_file *file);
int create_orc_sections(struct objtool_file *file);

int orc_dump(const char *objname);

#endif /* _ORC_H */
