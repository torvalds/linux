// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include "subcmd-util.h"
#include "help.h"
#include "exec-cmd.h"

void add_cmdname(struct cmdnames *cmds, const char *name, size_t len)
{
	struct cmdname *ent = malloc(sizeof(*ent) + len + 1);

	ent->len = len;
	memcpy(ent->name, name, len);
	ent->name[len] = 0;

	ALLOC_GROW(cmds->names, cmds->cnt + 1, cmds->alloc);
	cmds->names[cmds->cnt++] = ent;
}

void clean_cmdnames(struct cmdnames *cmds)
{
	unsigned int i;

	for (i = 0; i < cmds->cnt; ++i)
		zfree(&cmds->names[i]);
	zfree(&cmds->names);
	cmds->cnt = 0;
	cmds->alloc = 0;
}

int cmdname_compare(const void *a_, const void *b_)
{
	struct cmdname *a = *(struct cmdname **)a_;
	struct cmdname *b = *(struct cmdname **)b_;
	return strcmp(a->name, b->name);
}

void uniq(struct cmdnames *cmds)
{
	unsigned int i, j;

	if (!cmds->cnt)
		return;

	for (i = 1; i < cmds->cnt; i++) {
		if (!strcmp(cmds->names[i]->name, cmds->names[i-1]->name))
			zfree(&cmds->names[i - 1]);
	}
	for (i = 0, j = 0; i < cmds->cnt; i++) {
		if (cmds->names[i]) {
			if (i == j)
				j++;
			else
				cmds->names[j++] = cmds->names[i];
		}
	}
	cmds->cnt = j;
	while (j < i)
		cmds->names[j++] = NULL;
}

void exclude_cmds(struct cmdnames *cmds, struct cmdnames *excludes)
{
	size_t ci, cj, ei;
	int cmp;

	ci = cj = ei = 0;
	while (ci < cmds->cnt && ei < excludes->cnt) {
		cmp = strcmp(cmds->names[ci]->name, excludes->names[ei]->name);
		if (cmp < 0) {
			cmds->names[cj++] = cmds->names[ci++];
		} else if (cmp == 0) {
			ci++;
			ei++;
		} else if (cmp > 0) {
			ei++;
		}
	}

	while (ci < cmds->cnt)
		cmds->names[cj++] = cmds->names[ci++];

	cmds->cnt = cj;
}

static void get_term_dimensions(struct winsize *ws)
{
	char *s = getenv("LINES");

	if (s != NULL) {
		ws->ws_row = atoi(s);
		s = getenv("COLUMNS");
		if (s != NULL) {
			ws->ws_col = atoi(s);
			if (ws->ws_row && ws->ws_col)
				return;
		}
	}
#ifdef TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, ws) == 0 &&
	    ws->ws_row && ws->ws_col)
		return;
#endif
	ws->ws_row = 25;
	ws->ws_col = 80;
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

static int has_extension(const char *filename, const char *ext)
{
	size_t len = strlen(filename);
	size_t extlen = strlen(ext);

	return len > extlen && !memcmp(filename + len - extlen, ext, extlen);
}

static void list_commands_in_dir(struct cmdnames *cmds,
					 const char *path,
					 const char *prefix)
{
	int prefix_len;
	DIR *dir = opendir(path);
	struct dirent *de;
	char *buf = NULL;

	if (!dir)
		return;
	if (!prefix)
		prefix = "perf-";
	prefix_len = strlen(prefix);

	astrcatf(&buf, "%s/", path);

	while ((de = readdir(dir)) != NULL) {
		int entlen;

		if (!strstarts(de->d_name, prefix))
			continue;

		astrcat(&buf, de->d_name);
		if (!is_executable(buf))
			continue;

		entlen = strlen(de->d_name) - prefix_len;
		if (has_extension(de->d_name, ".exe"))
			entlen -= 4;

		add_cmdname(cmds, de->d_name + prefix_len, entlen);
	}
	closedir(dir);
	free(buf);
}

void load_command_list(const char *prefix,
		struct cmdnames *main_cmds,
		struct cmdnames *other_cmds)
{
	const char *env_path = getenv("PATH");
	char *exec_path = get_argv_exec_path();

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
			if ((colon = strchr(path, ':')))
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
	free(exec_path);
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
		char *exec_path = get_argv_exec_path();
		printf("available %s in '%s'\n", title, exec_path);
		printf("----------------");
		mput_char('-', strlen(title) + strlen(exec_path));
		putchar('\n');
		pretty_print_string_list(main_cmds, longest);
		putchar('\n');
		free(exec_path);
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
