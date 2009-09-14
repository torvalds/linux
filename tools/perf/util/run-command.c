#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"

static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

static inline void dup_devnull(int to)
{
	int fd = open("/dev/null", O_RDWR);
	dup2(fd, to);
	close(fd);
}

int start_command(struct child_process *cmd)
{
	int need_in, need_out, need_err;
	int fdin[2], fdout[2], fderr[2];

	/*
	 * In case of errors we must keep the promise to close FDs
	 * that have been passed in via ->in and ->out.
	 */

	need_in = !cmd->no_stdin && cmd->in < 0;
	if (need_in) {
		if (pipe(fdin) < 0) {
			if (cmd->out > 0)
				close(cmd->out);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->in = fdin[1];
	}

	need_out = !cmd->no_stdout
		&& !cmd->stdout_to_stderr
		&& cmd->out < 0;
	if (need_out) {
		if (pipe(fdout) < 0) {
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->out = fdout[0];
	}

	need_err = !cmd->no_stderr && cmd->err < 0;
	if (need_err) {
		if (pipe(fderr) < 0) {
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			if (need_out)
				close_pair(fdout);
			else if (cmd->out)
				close(cmd->out);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->err = fderr[0];
	}

	fflush(NULL);
	cmd->pid = fork();
	if (!cmd->pid) {
		if (cmd->no_stdin)
			dup_devnull(0);
		else if (need_in) {
			dup2(fdin[0], 0);
			close_pair(fdin);
		} else if (cmd->in) {
			dup2(cmd->in, 0);
			close(cmd->in);
		}

		if (cmd->no_stderr)
			dup_devnull(2);
		else if (need_err) {
			dup2(fderr[1], 2);
			close_pair(fderr);
		}

		if (cmd->no_stdout)
			dup_devnull(1);
		else if (cmd->stdout_to_stderr)
			dup2(2, 1);
		else if (need_out) {
			dup2(fdout[1], 1);
			close_pair(fdout);
		} else if (cmd->out > 1) {
			dup2(cmd->out, 1);
			close(cmd->out);
		}

		if (cmd->dir && chdir(cmd->dir))
			die("exec %s: cd to %s failed (%s)", cmd->argv[0],
			    cmd->dir, strerror(errno));
		if (cmd->env) {
			for (; *cmd->env; cmd->env++) {
				if (strchr(*cmd->env, '='))
					putenv((char*)*cmd->env);
				else
					unsetenv(*cmd->env);
			}
		}
		if (cmd->preexec_cb)
			cmd->preexec_cb();
		if (cmd->perf_cmd) {
			execv_perf_cmd(cmd->argv);
		} else {
			execvp(cmd->argv[0], (char *const*) cmd->argv);
		}
		exit(127);
	}

	if (cmd->pid < 0) {
		int err = errno;
		if (need_in)
			close_pair(fdin);
		else if (cmd->in)
			close(cmd->in);
		if (need_out)
			close_pair(fdout);
		else if (cmd->out)
			close(cmd->out);
		if (need_err)
			close_pair(fderr);
		return err == ENOENT ?
			-ERR_RUN_COMMAND_EXEC :
			-ERR_RUN_COMMAND_FORK;
	}

	if (need_in)
		close(fdin[0]);
	else if (cmd->in)
		close(cmd->in);

	if (need_out)
		close(fdout[1]);
	else if (cmd->out)
		close(cmd->out);

	if (need_err)
		close(fderr[1]);

	return 0;
}

static int wait_or_whine(pid_t pid)
{
	for (;;) {
		int status, code;
		pid_t waiting = waitpid(pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid failed (%s)", strerror(errno));
			return -ERR_RUN_COMMAND_WAITPID;
		}
		if (waiting != pid)
			return -ERR_RUN_COMMAND_WAITPID_WRONG_PID;
		if (WIFSIGNALED(status))
			return -ERR_RUN_COMMAND_WAITPID_SIGNAL;

		if (!WIFEXITED(status))
			return -ERR_RUN_COMMAND_WAITPID_NOEXIT;
		code = WEXITSTATUS(status);
		switch (code) {
		case 127:
			return -ERR_RUN_COMMAND_EXEC;
		case 0:
			return 0;
		default:
			return -code;
		}
	}
}

int finish_command(struct child_process *cmd)
{
	return wait_or_whine(cmd->pid);
}

int run_command(struct child_process *cmd)
{
	int code = start_command(cmd);
	if (code)
		return code;
	return finish_command(cmd);
}

static void prepare_run_command_v_opt(struct child_process *cmd,
				      const char **argv,
				      int opt)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->argv = argv;
	cmd->no_stdin = opt & RUN_COMMAND_NO_STDIN ? 1 : 0;
	cmd->perf_cmd = opt & RUN_PERF_CMD ? 1 : 0;
	cmd->stdout_to_stderr = opt & RUN_COMMAND_STDOUT_TO_STDERR ? 1 : 0;
}

int run_command_v_opt(const char **argv, int opt)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	return run_command(&cmd);
}

int run_command_v_opt_cd_env(const char **argv, int opt, const char *dir, const char *const *env)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	cmd.dir = dir;
	cmd.env = env;
	return run_command(&cmd);
}

int start_async(struct async *async)
{
	int pipe_out[2];

	if (pipe(pipe_out) < 0)
		return error("cannot create pipe: %s", strerror(errno));
	async->out = pipe_out[0];

	/* Flush stdio before fork() to avoid cloning buffers */
	fflush(NULL);

	async->pid = fork();
	if (async->pid < 0) {
		error("fork (async) failed: %s", strerror(errno));
		close_pair(pipe_out);
		return -1;
	}
	if (!async->pid) {
		close(pipe_out[0]);
		exit(!!async->proc(pipe_out[1], async->data));
	}
	close(pipe_out[1]);

	return 0;
}

int finish_async(struct async *async)
{
	int ret = 0;

	if (wait_or_whine(async->pid))
		ret = error("waitpid (async) failed");

	return ret;
}

int run_hook(const char *index_file, const char *name, ...)
{
	struct child_process hook;
	const char **argv = NULL, *env[2];
	char index[PATH_MAX];
	va_list args;
	int ret;
	size_t i = 0, alloc = 0;

	if (access(perf_path("hooks/%s", name), X_OK) < 0)
		return 0;

	va_start(args, name);
	ALLOC_GROW(argv, i + 1, alloc);
	argv[i++] = perf_path("hooks/%s", name);
	while (argv[i-1]) {
		ALLOC_GROW(argv, i + 1, alloc);
		argv[i++] = va_arg(args, const char *);
	}
	va_end(args);

	memset(&hook, 0, sizeof(hook));
	hook.argv = argv;
	hook.no_stdin = 1;
	hook.stdout_to_stderr = 1;
	if (index_file) {
		snprintf(index, sizeof(index), "PERF_INDEX_FILE=%s", index_file);
		env[0] = index;
		env[1] = NULL;
		hook.env = env;
	}

	ret = start_command(&hook);
	free(argv);
	if (ret) {
		warning("Could not spawn %s", argv[0]);
		return ret;
	}
	ret = finish_command(&hook);
	if (ret == -ERR_RUN_COMMAND_WAITPID_SIGNAL)
		warning("%s exited due to uncaught signal", argv[0]);

	return ret;
}
