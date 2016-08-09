#include "cache.h"
#include "config.h"
#include <stdio.h>
#include <subcmd/help.h>
#include "../builtin.h"
#include "levenshtein.h"

static int autocorrect;
static struct cmdnames aliases;

static int perf_unknown_cmd_config(const char *var, const char *value,
				   void *cb __maybe_unused)
{
	if (!strcmp(var, "help.autocorrect"))
		autocorrect = perf_config_int(var,value);
	/* Also use aliases for command lookup */
	if (!prefixcmp(var, "alias."))
		add_cmdname(&aliases, var + 6, strlen(var + 6));

	return 0;
}

static int levenshtein_compare(const void *p1, const void *p2)
{
	const struct cmdname *const *c1 = p1, *const *c2 = p2;
	const char *s1 = (*c1)->name, *s2 = (*c2)->name;
	int l1 = (*c1)->len;
	int l2 = (*c2)->len;
	return l1 != l2 ? l1 - l2 : strcmp(s1, s2);
}

static int add_cmd_list(struct cmdnames *cmds, struct cmdnames *old)
{
	unsigned int i, nr = cmds->cnt + old->cnt;
	void *tmp;

	if (nr > cmds->alloc) {
		/* Choose bigger one to alloc */
		if (alloc_nr(cmds->alloc) < nr)
			cmds->alloc = nr;
		else
			cmds->alloc = alloc_nr(cmds->alloc);
		tmp = realloc(cmds->names, cmds->alloc * sizeof(*cmds->names));
		if (!tmp)
			return -1;
		cmds->names = tmp;
	}
	for (i = 0; i < old->cnt; i++)
		cmds->names[cmds->cnt++] = old->names[i];
	zfree(&old->names);
	old->cnt = 0;
	return 0;
}

const char *help_unknown_cmd(const char *cmd)
{
	unsigned int i, n = 0, best_similarity = 0;
	struct cmdnames main_cmds, other_cmds;

	memset(&main_cmds, 0, sizeof(main_cmds));
	memset(&other_cmds, 0, sizeof(main_cmds));
	memset(&aliases, 0, sizeof(aliases));

	perf_config(perf_unknown_cmd_config, NULL);

	load_command_list("perf-", &main_cmds, &other_cmds);

	if (add_cmd_list(&main_cmds, &aliases) < 0 ||
	    add_cmd_list(&main_cmds, &other_cmds) < 0) {
		fprintf(stderr, "ERROR: Failed to allocate command list for unknown command.\n");
		goto end;
	}
	qsort(main_cmds.names, main_cmds.cnt,
	      sizeof(main_cmds.names), cmdname_compare);
	uniq(&main_cmds);

	if (main_cmds.cnt) {
		/* This reuses cmdname->len for similarity index */
		for (i = 0; i < main_cmds.cnt; ++i)
			main_cmds.names[i]->len =
				levenshtein(cmd, main_cmds.names[i]->name, 0, 2, 1, 4);

		qsort(main_cmds.names, main_cmds.cnt,
		      sizeof(*main_cmds.names), levenshtein_compare);

		best_similarity = main_cmds.names[0]->len;
		n = 1;
		while (n < main_cmds.cnt && best_similarity == main_cmds.names[n]->len)
			++n;
	}

	if (autocorrect && n == 1) {
		const char *assumed = main_cmds.names[0]->name;

		main_cmds.names[0] = NULL;
		clean_cmdnames(&main_cmds);
		fprintf(stderr, "WARNING: You called a perf program named '%s', "
			"which does not exist.\n"
			"Continuing under the assumption that you meant '%s'\n",
			cmd, assumed);
		if (autocorrect > 0) {
			fprintf(stderr, "in %0.1f seconds automatically...\n",
				(float)autocorrect/10.0);
			poll(NULL, 0, autocorrect * 100);
		}
		return assumed;
	}

	fprintf(stderr, "perf: '%s' is not a perf-command. See 'perf --help'.\n", cmd);

	if (main_cmds.cnt && best_similarity < 6) {
		fprintf(stderr, "\nDid you mean %s?\n",
			n < 2 ? "this": "one of these");

		for (i = 0; i < n; i++)
			fprintf(stderr, "\t%s\n", main_cmds.names[i]->name);
	}
end:
	exit(1);
}
