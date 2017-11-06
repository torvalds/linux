/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ORC_H
#define _ORC_H

#include <asm/orc_types.h>

struct objtool_file;

int create_orc(struct objtool_file *file);
int create_orc_sections(struct objtool_file *file);

int orc_dump(const char *objname);

#endif /* _ORC_H */
