// SPDX-License-Identifier: GPL-2.0-or-later
#include <subcmd/parse-options.h>
#include <string.h>
#include <stdlib.h>
#include <objtool/builtin.h>
#include <objtool/objtool.h>
#include <objtool/klp.h>

struct subcmd {
	const char *name;
	const char *description;
	int (*fn)(int, const char **);
};

static struct subcmd subcmds[] = {
	{ "diff",		"Generate binary diff of two object files",		cmd_klp_diff, },
	{ "post-link",		"Finalize klp symbols/relocs after module linking",	cmd_klp_post_link, },
};

static void cmd_klp_usage(void)
{
	fprintf(stderr, "usage: objtool klp <subcommand> [<options>]\n\n");
	fprintf(stderr, "Subcommands:\n");

	for (int i = 0; i < ARRAY_SIZE(subcmds); i++) {
		struct subcmd *cmd = &subcmds[i];

		fprintf(stderr, "  %s\t%s\n", cmd->name, cmd->description);
	}

	exit(1);
}

int cmd_klp(int argc, const char **argv)
{
	argc--;
	argv++;

	if (!argc)
		cmd_klp_usage();

	if (argc) {
		for (int i = 0; i < ARRAY_SIZE(subcmds); i++) {
			struct subcmd *cmd = &subcmds[i];

			if (!strcmp(cmd->name, argv[0]))
				return cmd->fn(argc, argv);
		}
	}

	cmd_klp_usage();
	return 0;
}
