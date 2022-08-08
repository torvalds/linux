// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Matt Helsley <mhelsley@vmware.com>
 * Weak definitions necessary to compile objtool without
 * some subcommands (e.g. check, orc).
 */

#include <stdbool.h>
#include <errno.h>
#include <objtool/objtool.h>

#define UNSUPPORTED(name)						\
({									\
	fprintf(stderr, "error: objtool: " name " not implemented\n");	\
	return ENOSYS;							\
})

int __weak check(struct objtool_file *file)
{
	UNSUPPORTED("check subcommand");
}

int __weak orc_dump(const char *_objname)
{
	UNSUPPORTED("orc");
}

int __weak orc_create(struct objtool_file *file)
{
	UNSUPPORTED("orc");
}
