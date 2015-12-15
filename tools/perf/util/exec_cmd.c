#include "cache.h"
#include "exec_cmd.h"
#include "quote.h"
#include "subcmd-config.h"

#include <string.h>

#define MAX_ARGS	32

static const char *argv_exec_path;
static const char *argv0_path;

void exec_cmd_init(const char *exec_name, const char *prefix,
		   const char *exec_path, const char *exec_path_env)
{
	subcmd_config.exec_name		= exec_name;
	subcmd_config.prefix		= prefix;
	subcmd_config.exec_path		= exec_path;
	subcmd_config.exec_path_env	= exec_path_env;
}

char *system_path(const char *path)
{
	struct strbuf d = STRBUF_INIT;

	if (is_absolute_path(path))
		return strdup(path);

	strbuf_addf(&d, "%s/%s", subcmd_config.prefix, path);
	path = strbuf_detach(&d, NULL);
	return (char *)path;
}

const char *perf_extract_argv0_path(const char *argv0)
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

void perf_set_argv_exec_path(const char *exec_path)
{
	argv_exec_path = exec_path;
	/*
	 * Propagate this setting to external programs.
	 */
	setenv(subcmd_config.exec_path_env, exec_path, 1);
}


/* Returns the highest-priority, location to look for perf programs. */
char *perf_exec_path(void)
{
	char *env;

	if (argv_exec_path)
		return strdup(argv_exec_path);

	env = getenv(subcmd_config.exec_path_env);
	if (env && *env)
		return strdup(env);

	return system_path(subcmd_config.exec_path);
}

static void add_path(struct strbuf *out, const char *path)
{
	if (path && *path) {
		if (is_absolute_path(path))
			strbuf_addstr(out, path);
		else
			strbuf_addstr(out, make_nonrelative_path(path));

		strbuf_addch(out, PATH_SEP);
	}
}

void setup_path(void)
{
	const char *old_path = getenv("PATH");
	struct strbuf new_path = STRBUF_INIT;
	char *tmp = perf_exec_path();

	add_path(&new_path, tmp);
	add_path(&new_path, argv0_path);
	free(tmp);

	if (old_path)
		strbuf_addstr(&new_path, old_path);
	else
		strbuf_addstr(&new_path, "/usr/local/bin:/usr/bin:/bin");

	setenv("PATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

static const char **prepare_perf_cmd(const char **argv)
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

int execv_perf_cmd(const char **argv) {
	const char **nargv = prepare_perf_cmd(argv);

	/* execvp() can only ever return if it fails */
	execvp(subcmd_config.exec_name, (char **)nargv);

	free(nargv);
	return -1;
}


int execl_perf_cmd(const char *cmd,...)
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
	if (MAX_ARGS <= argc)
		return error("too many args to run %s", cmd);

	argv[argc] = NULL;
	return execv_perf_cmd(argv);
}
