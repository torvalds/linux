// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

/*
 * objtool orc:
 *
 * This command analyzes a .o file and adds .orc_unwind and .orc_unwind_ip
 * sections to it, which is used by the in-kernel ORC unwinder.
 *
 * This command is a superset of "objtool check".
 */

#include <string.h>
#include "builtin.h"
#include "objtool.h"

static const char *orc_usage[] = {
	"objtool orc generate [<options>] file.o",
	"objtool orc dump file.o",
	NULL,
};

int cmd_orc(int argc, const char **argv)
{
	const char *objname;

	argc--; argv++;
	if (argc <= 0)
		usage_with_options(orc_usage, check_options);

	if (!strncmp(argv[0], "gen", 3)) {
		struct objtool_file *file;
		int ret;

		argc = parse_options(argc, argv, check_options, orc_usage, 0);
		if (argc != 1)
			usage_with_options(orc_usage, check_options);

		objname = argv[0];

		file = objtool_open_read(objname);
		if (!file)
			return 1;

		ret = check(file);
		if (ret)
			return ret;

		if (list_empty(&file->insn_list))
			return 0;

		ret = create_orc(file);
		if (ret)
			return ret;

		ret = create_orc_sections(file);
		if (ret)
			return ret;

		if (!file->elf->changed)
			return 0;

		return elf_write(file->elf);
	}

	if (!strcmp(argv[0], "dump")) {
		if (argc != 2)
			usage_with_options(orc_usage, check_options);

		objname = argv[1];

		return orc_dump(objname);
	}

	usage_with_options(orc_usage, check_options);

	return 0;
}
