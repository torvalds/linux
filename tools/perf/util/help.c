#include "cache.h"
#include "../builtin.h"
#include "exec_cmd.h"
#include "levenshtein.h"
#include "help.h"

void add_cmdname(struct cmdnames *cmds, const char *name, size_t len)
{
	struct cmdname *ent = malloc(sizeof(*ent) + len + 1);

	ent->len = len;
	memcpy(ent->name, name, len);
	ent->name[len] = 0;

	ALLOC_GROW(cmds->names, cmds->cnt + 1, cmds->alloc);
	cmds->names[cmds->cnt++] = ent;
}

static void clean_cmdnames(struct cmdnames *cmds)
{
	unsigned int i;

	for (i = 0; i < cmds->cnt; ++i)
		free(cmds->names[i]);
	free(cmds->names);
	cmds->cnt = 0;
	cmds->alloc = 0;
}

static int cmdname_compare(const void *a_, const void *b_)
{
	struct cmdname *a = *(struct cmdname **)a_;
	struct cmdname *b = *(struct cmdname **)b_;
	return strcmp(a->name, b->name);
}

static void uniq(struct cmdnames *cmds)
{
	unsigned int i, j;

	if (!cmds->cnt)
		return;

	for (i = j = 1; i < cmds->cnt; i++)
		if (strcmp(cmds->names[i]->name, cmds->names[i-1]->name))
			cmds->names[j++] = cmds->names[i];

	cmds->cnt = j;
}

void exclude_cmds(struct cmdnames *cmds, struct cmdnames *excludes)
{
	size_t ci, cj, ei;
	int cmp;

	ci = cj = ei = 0;
	while (ci < cmds->cnt && ei < excludes->cnt) {
		cmp = strcmp(cmds->names[ci]->name, excludes->names[ei]->name);
		if (cmp < 0)
			cmds->names[cj++] = cmds->names[ci++];
		else if (cmp == 0)
			ci++, ei++;
		else if (cmp > 0)
			ei++;
	}

	while (ci < cmds->cnt)
		cmds->names[cj++] = cmds->names[ci++];

	cmds->cnt = cj;
}

static void pretty_print_string_list(struct cmdnames *cmds, int longest)
{
	int cols = 1, rows;
	int space = longest + 1; /* min 1 SP between words */
	struct winsize win;
	int max_cols;
	int i, j;

	get_term_dimensions(&win);
	max_cols = win.ws_col - 1; /* don't print *on* the edge */

	if (space < max_cols)
		cols = max_cols / space;
	rows = (cmds->cnt + cols - 1) / cols;

	for (i = 0; i < rows; i++) {
		printf("  ");

		for (j = 0; j < cols; j++) {
			unsigned int n = j * rows + i;
			unsigned int size = space;

			if (n >= cmds->cnt)
				break;
			if (j == cols-1 || n + rows >= cmds->cnt)
				size = 1;
			printf("%-*s", size, cmds->names[n]->name);
		}
		putchar('\n');
	}
}

static int is_executable(const char *name)
{
	struct stat st;

	if (stat(name, &st) || /* stat, not lstat */
	    !S_ISREG(st.st_mode))
		return 0;

	return st.st_mode & S_IXUSR;
}

static void list_commands_in_dir(struct cmdnames *cmds,
					 const char *path,
					 const char *prefix)
{
	int prefix_len;
	DIR *dir = opendir(path);
	struct dirent *de;
	struct strbuf buf = STRBUF_INIT;
	int len;

	if (!dir)
		return;
	if (!prefix)
		prefix = "perf-";
	prefix_len = strlen(prefix);

	strbuf_addf(&buf, "%s/", path);
	len = buf.len;

	while ((de = readdir(dir)) != NULL) {
		int entlen;

		if (prefixcmp(de->d_name, prefix))
			continue;

		strbuf_setlen(&buf, len);
		strbuf_addstr(&buf, de->d_name);
		if (!is_executable(buf.buf))
			continue;

		entlen = strlen(de->d_name) - prefix_len;
		if (has_extension(de->d_name, ".exe"))
			entlen -= 4;

		add_cmdname(cmds, de->d_name + prefix_len, entlen);
	}
	closedir(dir);
	strbuf_release(&buf);
}

void load_command_list(const char *prefix,
		struct cmdnames *main_cmds,
		struct cmdnames *other_cmds)
{
	const char *env_path = getenv("PATH");
	const char *exec_path = perf_exec_path();

	if (exec_path) {
		list_commands_in_dir(main_cmds, exec_path, prefix);
		qsort(main_cmds->names, main_cmds->cnt,
		      sizeof(*main_cmds->names), cmdname_compare);
		uniq(main_cmds);
	}

	if (env_path) {
		char *paths, *path, *colon;
		path = paths = strdup(env_path);
		while (1) {
			if ((colon = strchr(path, PATH_SEP)))
				*colon = 0;
			if (!exec_path || strcmp(path, exec_path))
				list_commands_in_dir(other_cmds, path, prefix);

			if (!colon)
				break;
			path = colon + 1;
		}
		free(paths);

		qsort(other_cmds->names, other_cmds->cnt,
		      sizeof(*other_cmds->names), cmdname_compare);
		uniq(other_cmds);
	}
	exclude_cmds(other_cmds, main_cmds);
}

void list_commands(const char *title, struct cmdnames *main_cmds,
		   struct cmdnames *other_cmds)
{
	unsigned int i, longest = 0;

	for (i = 0; i < main_cmds->cnt; i++)
		if (longest < main_cmds->names[i]->len)
			longest = main_cmds->names[i]->len;
	for (i = 0; i < other_cmds->cnt; i++)
		if (longest < other_cmds->names[i]->len)
			longest = other_cmds->names[i]->len;

	if (main_cmds->cnt) {
		const char *exec_path = perf_exec_path();
		printf("available %s in '%s'\n", title, exec_path);
		printf("----------------");
		mput_char('-', strlen(title) + strlen(exec_path));
		putchar('\n');
		pretty_print_string_list(main_cmds, longest);
		putchar('\n');
	}

	if (other_cmds->cnt) {
		printf("%s available from elsewhere on your $PATH\n", title);
		printf("---------------------------------------");
		mput_char('-', strlen(title));
		putchar('\n');
		pretty_print_string_list(other_cmds, longest);
		putchar('\n');
	}
}

int is_in_cmdlist(struct cmdnames *c, const char *s)
{
	unsigned int i;

	for (i = 0; i < c->cnt; i++)
		if (!strcmp(s, c->names[i]->name))
			return 1;
	return 0;
}

static int autocorrect;
static struct cmdnames aliases;

static int perf_unknown_cmd_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "help.autocorrect"))
		autocorrect = perf_config_int(var,value);
	/* Also use aliases for command lookup */
	if (!prefixcmp(var, "alias."))
		add_cmdname(&aliases, var + 6, strlen(var + 6));

	return perf_default_config(var, value, cb);
}

static int levenshtein_compare(const void *p1, const void *p2)
{
	const struct cmdname *const *c1 = p1, *const *c2 = p2;
	const char *s1 = (*c1)->name, *s2 = (*c2)->name;
	int l1 = (*c1)->len;
	int l2 = (*c2)->len;
	return l1 != l2 ? l1 - l2 : strcmp(s1, s2);
}

static void add_cmd_list(struct cmdnames *cmds, struct cmdnames *old)
{
	unsigned int i;

	ALLOC_GROW(cmds->names, cmds->cnt + old->cnt, cmds->alloc);

	for (i = 0; i < old->cnt; i++)
		cmds->names[cmds->cnt++] = old->names[i];
	free(old->names);
	old->cnt = 0;
	old->names = NULL;
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

	add_cmd_list(&main_cmds, &aliases);
	add_cmd_list(&main_cmds, &other_cmds);
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

	exit(1);
}

int cmd_version(int argc __used, const char **argv __used, const char *prefix __used)
{
	printf("perf version %s\n", perf_version_string);
	return 0;
}
