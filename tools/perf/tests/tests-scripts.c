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

static int shell_tests__dir_fd(void)
{
	struct stat st;
	char path[PATH_MAX], path2[PATH_MAX], *exec_path;
	static const char * const devel_dirs[] = {
		"./tools/perf/tests/shell",
		"./tests/shell",
		"./source/tests/shell"
	};
	int fd;
	char *p;

	for (size_t i = 0; i < ARRAY_SIZE(devel_dirs); ++i) {
		fd = open(devel_dirs[i], O_PATH);

		if (fd >= 0)
			return fd;
	}

	/* Use directory of executable */
	if (readlink("/proc/self/exe", path2, sizeof path2) < 0)
		return -1;
	/* Follow another level of symlink if there */
	if (lstat(path2, &st) == 0 && (st.st_mode & S_IFMT) == S_IFLNK) {
		scnprintf(path, sizeof(path), path2);
		if (readlink(path, path2, sizeof path2) < 0)
			return -1;
	}
	/* Get directory */
	p = strrchr(path2, '/');
	if (p)
		*p = 0;
	scnprintf(path, sizeof(path), "%s/tests/shell", path2);
	fd = open(path, O_PATH);
	if (fd >= 0)
		return fd;
	scnprintf(path, sizeof(path), "%s/source/tests/shell", path2);
	fd = open(path, O_PATH);
	if (fd >= 0)
		return fd;

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

static int shell_test__run(struct test_suite *test, int subtest __maybe_unused)
{
	const char *file = test->priv;
	int err;
	char *cmd = NULL;

	if (asprintf(&cmd, "%s%s", file, verbose ? " -v" : "") < 0)
		return TEST_FAIL;
	err = system(cmd);
	free(cmd);
	if (!err)
		return TEST_OK;

	return WEXITSTATUS(err) == 2 ? TEST_SKIP : TEST_FAIL;
}

static void append_script(int dir_fd, const char *name, char *desc,
			  struct test_suite ***result,
			  size_t *result_sz)
{
	char filename[PATH_MAX], link[128];
	struct test_suite *test_suite, **result_tmp;
	struct test_case *tests;
	ssize_t len;
	char *exclusive;

	snprintf(link, sizeof(link), "/proc/%d/fd/%d", getpid(), dir_fd);
	len = readlink(link, filename, sizeof(filename));
	if (len < 0) {
		pr_err("Failed to readlink %s", link);
		return;
	}
	filename[len++] = '/';
	strcpy(&filename[len], name);

	tests = calloc(2, sizeof(*tests));
	if (!tests) {
		pr_err("Out of memory while building script test suite list\n");
		return;
	}
	tests[0].name = strdup_check(name);
	exclusive = strstr(desc, " (exclusive)");
	if (exclusive != NULL) {
		tests[0].exclusive = true;
		exclusive[0] = '\0';
	}
	tests[0].desc = strdup_check(desc);
	tests[0].run_case = shell_test__run;
	test_suite = zalloc(sizeof(*test_suite));
	if (!test_suite) {
		pr_err("Out of memory while building script test suite list\n");
		free(tests);
		return;
	}
	test_suite->desc = desc;
	test_suite->test_cases = tests;
	test_suite->priv = strdup_check(filename);
	/* Realloc is good enough, though we could realloc by chunks, not that
	 * anyone will ever measure performance here */
	result_tmp = realloc(*result, (*result_sz + 1) * sizeof(*result_tmp));
	if (result_tmp == NULL) {
		pr_err("Out of memory while building script test suite list\n");
		free(tests);
		free(test_suite);
		return;
	}
	/* Add file to end and NULL terminate the struct array */
	*result = result_tmp;
	(*result)[*result_sz] = test_suite;
	(*result_sz)++;
}

static void append_scripts_in_dir(int dir_fd,
				  struct test_suite ***result,
				  size_t *result_sz)
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
				append_script(dir_fd, ent->d_name, desc, result, result_sz);
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
		if (strncmp(ent->d_name, "base_", 5) == 0)
			continue; /* Skip scripts that have a separate driver. */
		fd = openat(dir_fd, ent->d_name, O_PATH);
		append_scripts_in_dir(fd, result, result_sz);
	}
	for (i = 0; i < n_dirs; i++) /* Clean up */
		zfree(&entlist[i]);
	free(entlist);
}

struct test_suite **create_script_test_suites(void)
{
	struct test_suite **result = NULL, **result_tmp;
	size_t result_sz = 0;
	int dir_fd = shell_tests__dir_fd(); /* Walk  dir */

	/*
	 * Append scripts if fd is good, otherwise return a NULL terminated zero
	 * length array.
	 */
	if (dir_fd >= 0)
		append_scripts_in_dir(dir_fd, &result, &result_sz);

	result_tmp = realloc(result, (result_sz + 1) * sizeof(*result_tmp));
	if (result_tmp == NULL) {
		pr_err("Out of memory while building script test suite list\n");
		abort();
	}
	/* NULL terminate the test suite array. */
	result = result_tmp;
	result[result_sz] = NULL;
	if (dir_fd >= 0)
		close(dir_fd);
	return result;
}
