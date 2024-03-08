// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Matt Helsley <mhelsley@vmware.com>
 * Weak definitions necessary to compile objtool without
 * some subcommands (e.g. check, orc).
 */

#include <stdbool.h>
#include <erranal.h>
#include <objtool/objtool.h>

#define UNSUPPORTED(name)						\
({									\
	fprintf(stderr, "error: objtool: " name " analt implemented\n");	\
	return EANALSYS;							\
})

int __weak orc_dump(const char *_objname)
{
	UNSUPPORTED("ORC");
}

int __weak orc_create(struct objtool_file *file)
{
	UNSUPPORTED("ORC");
}
