// SPDX-License-Identifier: BSD-3-Clause
/*
 * Very simple script interpreter that can evaluate two different commands (one
 * per line):
 * - "?" to initialize a counter from user's input;
 * - "+" to increment the counter (which is set to 0 by default).
 *
 * See tools/testing/selftests/exec/check-exec-tests.sh and
 * Documentation/userspace-api/check_exec.rst
 *
 * Copyright © 2024 Microsoft Corporation
 */

#define _GNU_SOURCE
#include <errno.h>
#include <linux/fcntl.h>
#include <linux/prctl.h>
#include <linux/securebits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int sys_execveat(int dirfd, const char *pathname, char *const argv[],
			char *const envp[], int flags)
{
	return syscall(__NR_execveat, dirfd, pathname, argv, envp, flags);
}

/* Returns 1 on error, 0 otherwise. */
static int interpret_buffer(char *buffer, size_t buffer_size)
{
	char *line, *saveptr = NULL;
	long long number = 0;

	/* Each command is the first character of a line. */
	saveptr = NULL;
	line = strtok_r(buffer, "\n", &saveptr);
	while (line) {
		if (*line != '#' && strlen(line) != 1) {
			fprintf(stderr, "# ERROR: Unknown string\n");
			return 1;
		}
		switch (*line) {
		case '#':
			/* Skips shebang and comments. */
			break;
		case '+':
			/* Increments and prints the number. */
			number++;
			printf("%lld\n", number);
			break;
		case '?':
			/* Reads integer from stdin. */
			fprintf(stderr, "> Enter new number: \n");
			if (scanf("%lld", &number) != 1) {
				fprintf(stderr,
					"# WARNING: Failed to read number from stdin\n");
			}
			break;
		default:
			fprintf(stderr, "# ERROR: Unknown character '%c'\n",
				*line);
			return 1;
		}
		line = strtok_r(NULL, "\n", &saveptr);
	}
	return 0;
}

/* Returns 1 on error, 0 otherwise. */
static int interpret_stream(FILE *script, char *const script_name,
			    char *const *const envp, const bool restrict_stream)
{
	int err;
	char *const script_argv[] = { script_name, NULL };
	char buf[128] = {};
	size_t buf_size = sizeof(buf);

	/*
	 * We pass a valid argv and envp to the kernel to emulate a native
	 * script execution.  We must use the script file descriptor instead of
	 * the script path name to avoid race conditions.
	 */
	err = sys_execveat(fileno(script), "", script_argv, envp,
			   AT_EMPTY_PATH | AT_EXECVE_CHECK);
	if (err && restrict_stream) {
		perror("ERROR: Script execution check");
		return 1;
	}

	/* Reads script. */
	buf_size = fread(buf, 1, buf_size - 1, script);
	return interpret_buffer(buf, buf_size);
}

static void print_usage(const char *argv0)
{
	fprintf(stderr, "usage: %s <script.inc> | -i | -c <command>\n\n",
		argv0);
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "  ./set-exec -fi -- ./inc -i < script-exec.inc\n");
}

int main(const int argc, char *const argv[], char *const *const envp)
{
	int opt;
	char *cmd = NULL;
	char *script_name = NULL;
	bool interpret_stdin = false;
	FILE *script_file = NULL;
	int secbits;
	bool deny_interactive, restrict_file;
	size_t arg_nb;

	secbits = prctl(PR_GET_SECUREBITS);
	if (secbits == -1) {
		/*
		 * This should never happen, except with a buggy seccomp
		 * filter.
		 */
		perror("ERROR: Failed to get securebits");
		return 1;
	}

	deny_interactive = !!(secbits & SECBIT_EXEC_DENY_INTERACTIVE);
	restrict_file = !!(secbits & SECBIT_EXEC_RESTRICT_FILE);

	while ((opt = getopt(argc, argv, "c:i")) != -1) {
		switch (opt) {
		case 'c':
			if (cmd) {
				fprintf(stderr, "ERROR: Command already set");
				return 1;
			}
			cmd = optarg;
			break;
		case 'i':
			interpret_stdin = true;
			break;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	/* Checks that only one argument is used, or read stdin. */
	arg_nb = !!cmd + !!interpret_stdin;
	if (arg_nb == 0 && argc == 2) {
		script_name = argv[1];
	} else if (arg_nb != 1) {
		print_usage(argv[0]);
		return 1;
	}

	if (cmd) {
		/*
		 * Other kind of interactive interpretations should be denied
		 * as well (e.g. CLI arguments passing script snippets,
		 * environment variables interpreted as script).  However, any
		 * way to pass script files should only be restricted according
		 * to restrict_file.
		 */
		if (deny_interactive) {
			fprintf(stderr,
				"ERROR: Interactive interpretation denied.\n");
			return 1;
		}

		return interpret_buffer(cmd, strlen(cmd));
	}

	if (interpret_stdin && !script_name) {
		script_file = stdin;
		/*
		 * As for any execve(2) call, this path may be logged by the
		 * kernel.
		 */
		script_name = "/proc/self/fd/0";
		/*
		 * When stdin is used, it can point to a regular file or a
		 * pipe.  Restrict stdin execution according to
		 * SECBIT_EXEC_DENY_INTERACTIVE but always allow executable
		 * files (which are not considered as interactive inputs).
		 */
		return interpret_stream(script_file, script_name, envp,
					deny_interactive);
	} else if (script_name && !interpret_stdin) {
		/*
		 * In this sample, we don't pass any argument to scripts, but
		 * otherwise we would have to forge an argv with such
		 * arguments.
		 */
		script_file = fopen(script_name, "r");
		if (!script_file) {
			perror("ERROR: Failed to open script");
			return 1;
		}
		/*
		 * Restricts file execution according to
		 * SECBIT_EXEC_RESTRICT_FILE.
		 */
		return interpret_stream(script_file, script_name, envp,
					restrict_file);
	}

	print_usage(argv[0]);
	return 1;
}
