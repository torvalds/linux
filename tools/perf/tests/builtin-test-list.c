/* SPDX-License-Identifier: GPL-2.0 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <subcmd/exec-cmd.h>
#include <subcmd/parse-options.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "builtin.h"
#include "builtin-test-list.h"
#include "color.h"
#include "debug.h"
#include "hist.h"
#include "intlist.h"
#include "string2.h"
#include "symbol.h"
#include "tests.h"
#include "util/rlimit.h"


/*
 * As this is a singleton built once for the run of the process, there is
 * no value in trying to free it and just let it stay around until process
 * exits when it's cleaned up.
 */
static size_t files_num = 0;
static struct script_file *files = NULL;
static int files_max_width = 0;

static const char *shell_tests__dir(char *path, size_t size)
{
	const char *devel_dirs[] = { "./tools/perf/tests", "./tests", };
	char *exec_path;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(devel_dirs); ++i) {
		struct stat st;

		if (!lstat(devel_dirs[i], &st)) {
			scnprintf(path, size, "%s/shell", devel_dirs[i]);
			if (!lstat(devel_dirs[i], &st))
				return path;
		}
	}

	/* Then installed path. */
	exec_path = get_argv_exec_path();
	scnprintf(path, size, "%s/tests/shell", exec_path);
	free(exec_path);
	return path;
}

static const char *shell_test__description(char *description, size_t size,
                                           const char *path, const char *name)
{
	FILE *fp;
	char filename[PATH_MAX];
	int ch;

	path__join(filename, sizeof(filename), path, name);
	fp = fopen(filename, "r");
	if (!fp)
		return NULL;

	/* Skip first line - should be #!/bin/sh Shebang */
	do {
		ch = fgetc(fp);
	} while (ch != EOF && ch != '\n');

	description = fgets(description, size, fp);
	fclose(fp);

	/* Assume first char on line is omment everything after that desc */
	return description ? strim(description + 1) : NULL;
}

/* Is this full file path a shell script */
static bool is_shell_script(const char *path)
{
	const char *ext;

	ext = strrchr(path, '.');
	if (!ext)
		return false;
	if (!strcmp(ext, ".sh")) { /* Has .sh extension */
		if (access(path, R_OK | X_OK) == 0) /* Is executable */
			return true;
	}
	return false;
}

/* Is this file in this dir a shell script (for test purposes) */
static bool is_test_script(const char *path, const char *name)
{
	char filename[PATH_MAX];

	path__join(filename, sizeof(filename), path, name);
	if (!is_shell_script(filename)) return false;
	return true;
}

/* Duplicate a string and fall over and die if we run out of memory */
static char *strdup_check(const char *str)
{
	char *newstr;

	newstr = strdup(str);
	if (!newstr) {
		pr_err("Out of memory while duplicating test script string\n");
		abort();
	}
	return newstr;
}

static void append_script(const char *dir, const char *file, const char *desc)
{
	struct script_file *files_tmp;
	size_t files_num_tmp;
	int width;

	files_num_tmp = files_num + 1;
	if (files_num_tmp >= SIZE_MAX) {
		pr_err("Too many script files\n");
		abort();
	}
	/* Realloc is good enough, though we could realloc by chunks, not that
	 * anyone will ever measure performance here */
	files_tmp = realloc(files,
			    (files_num_tmp + 1) * sizeof(struct script_file));
	if (files_tmp == NULL) {
		pr_err("Out of memory while building test list\n");
		abort();
	}
	/* Add file to end and NULL terminate the struct array */
	files = files_tmp;
	files_num = files_num_tmp;
	files[files_num - 1].dir = strdup_check(dir);
	files[files_num - 1].file = strdup_check(file);
	files[files_num - 1].desc = strdup_check(desc);
	files[files_num].dir = NULL;
	files[files_num].file = NULL;
	files[files_num].desc = NULL;

	width = strlen(desc); /* Track max width of desc */
	if (width > files_max_width)
		files_max_width = width;
}

static void append_scripts_in_dir(const char *path)
{
	struct dirent **entlist;
	struct dirent *ent;
	int n_dirs, i;
	char filename[PATH_MAX];

	/* List files, sorted by alpha */
	n_dirs = scandir(path, &entlist, NULL, alphasort);
	if (n_dirs == -1)
		return;
	for (i = 0; i < n_dirs && (ent = entlist[i]); i++) {
		if (ent->d_name[0] == '.')
			continue; /* Skip hidden files */
		if (is_test_script(path, ent->d_name)) { /* It's a test */
			char bf[256];
			const char *desc = shell_test__description
				(bf, sizeof(bf), path, ent->d_name);

			if (desc) /* It has a desc line - valid script */
				append_script(path, ent->d_name, desc);
		} else if (is_directory(path, ent)) { /* Scan the subdir */
			path__join(filename, sizeof(filename),
				   path, ent->d_name);
			append_scripts_in_dir(filename);
		}
	}
	for (i = 0; i < n_dirs; i++) /* Clean up */
		zfree(&entlist[i]);
	free(entlist);
}

const struct script_file *list_script_files(void)
{
	char path_dir[PATH_MAX];
	const char *path;

	if (files)
		return files; /* Singleton - we already know our list */

	path = shell_tests__dir(path_dir, sizeof(path_dir)); /* Walk  dir */
	append_scripts_in_dir(path);

	return files;
}

int list_script_max_width(void)
{
	list_script_files(); /* Ensure we have scanned all scripts */
	return files_max_width;
}
