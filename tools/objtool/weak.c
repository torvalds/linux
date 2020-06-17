// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Matt Helsley <mhelsley@vmware.com>
 * Weak definitions necessary to compile objtool without
 * some subcommands (e.g. check, orc).
 */

#include <stdbool.h>
#include <errno.h>
#include "objtool.h"

#define __weak __attribute__((weak))

#define UNSUPPORTED(name)						\
({									\
	fprintf(stderr, "error: objtool: " name " not implemented\n");	\
	return ENOSYS;							\
})

const char __weak *objname;

int __weak check(const char *_objname, bool orc)
{
	UNSUPPORTED("check subcommand");
}

int __weak orc_dump(const char *_objname)
{
	UNSUPPORTED("orc");
}

int __weak create_orc(struct objtool_file *file)
{
	UNSUPPORTED("orc");
}

int __weak create_orc_sections(struct objtool_file *file)
{
	UNSUPPORTED("orc");
}
