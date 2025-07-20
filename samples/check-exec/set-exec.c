// SPDX-License-Identifier: BSD-3-Clause
/*
 * Simple tool to set SECBIT_EXEC_RESTRICT_FILE, SECBIT_EXEC_DENY_INTERACTIVE,
 * before executing a command.
 *
 * Copyright © 2024 Microsoft Corporation
 */

#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__
#include <errno.h>
#include <linux/prctl.h>
#include <linux/securebits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

static void print_usage(const char *argv0)
{
	fprintf(stderr, "usage: %s -f|-i -- <cmd> [args]...\n\n", argv0);
	fprintf(stderr, "Execute a command with\n");
	fprintf(stderr, "- SECBIT_EXEC_RESTRICT_FILE set: -f\n");
	fprintf(stderr, "- SECBIT_EXEC_DENY_INTERACTIVE set: -i\n");
}

int main(const int argc, char *const argv[], char *const *const envp)
{
	const char *cmd_path;
	char *const *cmd_argv;
	int opt, secbits_cur, secbits_new;
	bool has_policy = false;

	secbits_cur = prctl(PR_GET_SECUREBITS);
	if (secbits_cur == -1) {
		/*
		 * This should never happen, except with a buggy seccomp
		 * filter.
		 */
		perror("ERROR: Failed to get securebits");
		return 1;
	}

	secbits_new = secbits_cur;
	while ((opt = getopt(argc, argv, "fi")) != -1) {
		switch (opt) {
		case 'f':
			secbits_new |= SECBIT_EXEC_RESTRICT_FILE |
				       SECBIT_EXEC_RESTRICT_FILE_LOCKED;
			has_policy = true;
			break;
		case 'i':
			secbits_new |= SECBIT_EXEC_DENY_INTERACTIVE |
				       SECBIT_EXEC_DENY_INTERACTIVE_LOCKED;
			has_policy = true;
			break;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	if (!argv[optind] || !has_policy) {
		print_usage(argv[0]);
		return 1;
	}

	if (secbits_cur != secbits_new &&
	    prctl(PR_SET_SECUREBITS, secbits_new)) {
		perror("Failed to set secure bit(s).");
		fprintf(stderr,
			"Hint: The running kernel may not support this feature.\n");
		return 1;
	}

	cmd_path = argv[optind];
	cmd_argv = argv + optind;
	fprintf(stderr, "Executing command...\n");
	execvpe(cmd_path, cmd_argv, envp);
	fprintf(stderr, "Failed to execute \"%s\": %s\n", cmd_path,
		strerror(errno));
	return 1;
}
