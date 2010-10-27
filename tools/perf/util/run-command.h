#ifndef __PERF_RUN_COMMAND_H
#define __PERF_RUN_COMMAND_H

enum {
	ERR_RUN_COMMAND_FORK = 10000,
	ERR_RUN_COMMAND_EXEC,
	ERR_RUN_COMMAND_PIPE,
	ERR_RUN_COMMAND_WAITPID,
	ERR_RUN_COMMAND_WAITPID_WRONG_PID,
	ERR_RUN_COMMAND_WAITPID_SIGNAL,
	ERR_RUN_COMMAND_WAITPID_NOEXIT,
};
#define IS_RUN_COMMAND_ERR(x) (-(x) >= ERR_RUN_COMMAND_FORK)

struct child_process {
	const char **argv;
	pid_t pid;
	/*
	 * Using .in, .out, .err:
	 * - Specify 0 for no redirections (child inherits stdin, stdout,
	 *   stderr from parent).
	 * - Specify -1 to have a pipe allocated as follows:
	 *     .in: returns the writable pipe end; parent writes to it,
	 *          the readable pipe end becomes child's stdin
	 *     .out, .err: returns the readable pipe end; parent reads from
	 *          it, the writable pipe end becomes child's stdout/stderr
	 *   The caller of start_command() must close the returned FDs
	 *   after it has completed reading from/writing to it!
	 * - Specify > 0 to set a channel to a particular FD as follows:
	 *     .in: a readable FD, becomes child's stdin
	 *     .out: a writable FD, becomes child's stdout/stderr
	 *     .err > 0 not supported
	 *   The specified FD is closed by start_command(), even in case
	 *   of errors!
	 */
	int in;
	int out;
	int err;
	const char *dir;
	const char *const *env;
	unsigned no_stdin:1;
	unsigned no_stdout:1;
	unsigned no_stderr:1;
	unsigned perf_cmd:1; /* if this is to be perf sub-command */
	unsigned stdout_to_stderr:1;
	void (*preexec_cb)(void);
};

int start_command(struct child_process *);
int finish_command(struct child_process *);
int run_command(struct child_process *);

#define RUN_COMMAND_NO_STDIN 1
#define RUN_PERF_CMD	     2	/*If this is to be perf sub-command */
#define RUN_COMMAND_STDOUT_TO_STDERR 4
int run_command_v_opt(const char **argv, int opt);

#endif /* __PERF_RUN_COMMAND_H */
