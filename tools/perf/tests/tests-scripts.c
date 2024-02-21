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
#include <api/io.h>
#include "builtin.h"
#include "tests-scripts.h"
#include "color.h"
#include "debug.h"
#include "hist.h"
#include "intlist.h"
#include "string2.h"
#include "symbol.h"
#include "tests.h"
#include "util/rlimit.h"
#include "util/util.h"


/*
 * As this is a singleton built once for the run of the process, there is
 * no value in trying to free it and just let it stay around until process
 * exits when it's cleaned up.
 */
static size_t files_num = 0;
static struct script_file *files = NULL;
static int files_max_width = 0;

static int shell_tests__dir_fd(void)
{
	char path[PATH_MAX], *exec_path;
	static const char * const devel_dirs[] = { "./tools/perf/tests/shell", "./tests/shell", };

	for (size_t i = 0; i < ARRAY_SIZE(devel_dirs); ++i) {
		int fd = open(devel_dirs[i], O_PATH);

		if (fd >= 0)
			return fd;
	}

	/* Then installed path. */
	exec_path = get_argv_exec_path();
	scnprintf(path, sizeof(path), "%s/tests/shell", exec_path);
	free(exec_path);
	return open(path, O_PATH);
}

static char *shell_test__description(int dir_fd, const char *name)
{
	struct io io;
	char buf[128], desc[256];
	int ch, pos = 0;

	io__init(&io, openat(dir_fd, name, O_RDONLY), buf, sizeof(buf));
	if (io.fd < 0)
		return NULL;

	/* Skip first line - should be #!/bin/sh Shebang */
	if (io__get_char(&io) != '#')
		goto err_out;
	if (io__get_char(&io) != '!')
		goto err_out;
	do {
		ch = io__get_char(&io);
		if (ch < 0)
			goto err_out;
	} while (ch != '\n');

	do {
		ch = io__get_char(&io);
		if (ch < 0)
			goto err_out;
	} while (ch == '#' || isspace(ch));
	while (ch > 0 && ch != '\n') {
		desc[pos++] = ch;
		if (pos >= (int)sizeof(desc) - 1)
			break;
		ch = io__get_char(&io);
	}
	while (pos > 0 && isspace(desc[--pos]))
		;
	desc[++pos] = '\0';
	close(io.fd);
	return strdup(desc);
err_out:
	close(io.fd);
	return NULL;
}

/* Is this full file path a shell script */
static bool is_shell_script(int dir_fd, const char *path)
{
	const char *ext;

	ext = strrchr(path, '.');
	if (!ext)
		return false;
	if (!strcmp(ext, ".sh")) { /* Has .sh extension */
		if (faccessat(dir_fd, path, R_OK | X_OK, 0) == 0) /* Is executable */
			return true;
	}
	return false;
}

/* Is this file in this dir a shell script (for test purposes) */
static bool is_test_script(int dir_fd, const char *name)
{
	return is_shell_script(dir_fd, name);
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

static void append_script(int dir_fd, const char *name, char *desc)
{
	char filename[PATH_MAX], link[128];
	struct script_file *files_tmp;
	size_t files_num_tmp, len;
	int width;

	snprintf(link, sizeof(link), "/proc/%d/fd/%d", getpid(), dir_fd);
	len = readlink(link, filename, sizeof(filename));
	if (len < 0) {
		pr_err("Failed to readlink %s", link);
		return;
	}
	filename[len++] = '/';
	strcpy(&filename[len], name);
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
	files[files_num - 1].file = strdup_check(filename);
	files[files_num - 1].desc = desc;
	files[files_num].file = NULL;
	files[files_num].desc = NULL;

	width = strlen(desc); /* Track max width of desc */
	if (width > files_max_width)
		files_max_width = width;
}

static void append_scripts_in_dir(int dir_fd)
{
	struct dirent **entlist;
	struct dirent *ent;
	int n_dirs, i;

	/* List files, sorted by alpha */
	n_dirs = scandirat(dir_fd, ".", &entlist, NULL, alphasort);
	if (n_dirs == -1)
		return;
	for (i = 0; i < n_dirs && (ent = entlist[i]); i++) {
		int fd;

		if (ent->d_name[0] == '.')
			continue; /* Skip hidden files */
		if (is_test_script(dir_fd, ent->d_name)) { /* It's a test */
			char *desc = shell_test__description(dir_fd, ent->d_name);

			if (desc) /* It has a desc line - valid script */
				append_script(dir_fd, ent->d_name, desc);
			continue;
		}
		if (ent->d_type != DT_DIR) {
			struct stat st;

			if (ent->d_type != DT_UNKNOWN)
				continue;
			fstatat(dir_fd, ent->d_name, &st, 0);
			if (!S_ISDIR(st.st_mode))
				continue;
		}
		fd = openat(dir_fd, ent->d_name, O_PATH);
		append_scripts_in_dir(fd);
	}
	for (i = 0; i < n_dirs; i++) /* Clean up */
		zfree(&entlist[i]);
	free(entlist);
}

const struct script_file *list_script_files(void)
{
	int dir_fd;

	if (files)
		return files; /* Singleton - we already know our list */

	dir_fd = shell_tests__dir_fd(); /* Walk  dir */
	if (dir_fd < 0)
		return NULL;

	append_scripts_in_dir(dir_fd);
	close(dir_fd);

	return files;
}

int list_script_max_width(void)
{
	list_script_files(); /* Ensure we have scanned all scripts */
	return files_max_width;
}
