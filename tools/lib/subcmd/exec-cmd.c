// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "subcmd-util.h"
#include "exec-cmd.h"
#include "subcmd-config.h"

#define MAX_ARGS	32
#define PATH_MAX	4096

static const char *argv_exec_path;
static const char *argv0_path;

void exec_cmd_init(const char *exec_name, const char *prefix,
		   const char *exec_path, const char *exec_path_env)
{
	subcmd_config.exec_name		= exec_name;
	subcmd_config.prefix		= prefix;
	subcmd_config.exec_path		= exec_path;
	subcmd_config.exec_path_env	= exec_path_env;

	/* Setup environment variable for invoked shell script. */
	setenv("PREFIX", prefix, 1);
}

#define is_dir_sep(c) ((c) == '/')

static int is_absolute_path(const char *path)
{
	return path[0] == '/';
}

static const char *get_pwd_cwd(char *buf, size_t sz)
{
	char *pwd;
	struct stat cwd_stat, pwd_stat;
	if (getcwd(buf, sz) == NULL)
		return NULL;
	pwd = getenv("PWD");
	if (pwd && strcmp(pwd, buf)) {
		stat(buf, &cwd_stat);
		if (!stat(pwd, &pwd_stat) &&
		    pwd_stat.st_dev == cwd_stat.st_dev &&
		    pwd_stat.st_ino == cwd_stat.st_ino) {
			strlcpy(buf, pwd, sz);
		}
	}
	return buf;
}

static const char *make_nonrelative_path(char *buf, size_t sz, const char *path)
{
	if (is_absolute_path(path)) {
		if (strlcpy(buf, path, sz) >= sz)
			die("Too long path: %.*s", 60, path);
	} else {
		const char *cwd = get_pwd_cwd(buf, sz);

		if (!cwd)
			die("Cannot determine the current working directory");

		if (strlen(cwd) + strlen(path) + 2 >= sz)
			die("Too long path: %.*s", 60, path);

		strcat(buf, "/");
		strcat(buf, path);
	}
	return buf;
}

char *system_path(const char *path)
{
	char *buf = NULL;

	if (is_absolute_path(path))
		return strdup(path);

	astrcatf(&buf, "%s/%s", subcmd_config.prefix, path);

	return buf;
}

const char *extract_argv0_path(const char *argv0)
{
	const char *slash;

	if (!argv0 || !*argv0)
		return NULL;
	slash = argv0 + strlen(argv0);

	while (argv0 <= slash && !is_dir_sep(*slash))
		slash--;

	if (slash >= argv0) {
		argv0_path = strndup(argv0, slash - argv0);
		return argv0_path ? slash + 1 : NULL;
	}

	return argv0;
}

void set_argv_exec_path(const char *exec_path)
{
	argv_exec_path = exec_path;
	/*
	 * Propagate this setting to external programs.
	 */
	setenv(subcmd_config.exec_path_env, exec_path, 1);
}


/* Returns the highest-priority location to look for subprograms. */
char *get_argv_exec_path(void)
{
	char *env;

	if (argv_exec_path)
		return strdup(argv_exec_path);

	env = getenv(subcmd_config.exec_path_env);
	if (env && *env)
		return strdup(env);

	return system_path(subcmd_config.exec_path);
}

static void add_path(char **out, const char *path)
{
	if (path && *path) {
		if (is_absolute_path(path))
			astrcat(out, path);
		else {
			char buf[PATH_MAX];

			astrcat(out, make_nonrelative_path(buf, sizeof(buf), path));
		}

		astrcat(out, ":");
	}
}

void setup_path(void)
{
	const char *old_path = getenv("PATH");
	char *new_path = NULL;
	char *tmp = get_argv_exec_path();

	add_path(&new_path, tmp);
	add_path(&new_path, argv0_path);
	free(tmp);

	if (old_path)
		astrcat(&new_path, old_path);
	else
		astrcat(&new_path, "/usr/local/bin:/usr/bin:/bin");

	setenv("PATH", new_path, 1);

	free(new_path);
}

static const char **prepare_exec_cmd(const char **argv)
{
	int argc;
	const char **nargv;

	for (argc = 0; argv[argc]; argc++)
		; /* just counting */
	nargv = malloc(sizeof(*nargv) * (argc + 2));

	nargv[0] = subcmd_config.exec_name;
	for (argc = 0; argv[argc]; argc++)
		nargv[argc + 1] = argv[argc];
	nargv[argc + 1] = NULL;
	return nargv;
}

int execv_cmd(const char **argv) {
	const char **nargv = prepare_exec_cmd(argv);

	/* execvp() can only ever return if it fails */
	execvp(subcmd_config.exec_name, (char **)nargv);

	free(nargv);
	return -1;
}


int execl_cmd(const char *cmd,...)
{
	int argc;
	const char *argv[MAX_ARGS + 1];
	const char *arg;
	va_list param;

	va_start(param, cmd);
	argv[0] = cmd;
	argc = 1;
	while (argc < MAX_ARGS) {
		arg = argv[argc++] = va_arg(param, char *);
		if (!arg)
			break;
	}
	va_end(param);
	if (MAX_ARGS <= argc) {
		fprintf(stderr, " Error: too many args to run %s\n", cmd);
		return -1;
	}

	argv[argc] = NULL;
	return execv_cmd(argv);
}
