// SPDX-License-Identifier: GPL-2.0
/*
 * Write in a pipe, wait, sandbox itself, test sandboxing, and wait again.
 *
 * Used by audit_exec.flags from audit_test.c
 *
 * Copyright © 2024-2025 Microsoft Corporation
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/landlock.h>
#include <linux/prctl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "wrappers.h"

static int sync_with(int pipe_child, int pipe_parent)
{
	char buf;

	/* Signals that we are waiting. */
	if (write(pipe_child, ".", 1) != 1) {
		perror("Failed to write to first argument");
		return 1;
	}

	/* Waits for the parent do its test. */
	if (read(pipe_parent, &buf, 1) != 1) {
		perror("Failed to write to the second argument");
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	const struct landlock_ruleset_attr layer2 = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
	};
	const struct landlock_ruleset_attr layer3 = {
		.scoped = LANDLOCK_SCOPE_SIGNAL,
	};
	int err, pipe_child, pipe_parent, ruleset_fd;

	/* The first argument must be the file descriptor number of a pipe. */
	if (argc != 3) {
		fprintf(stderr, "Wrong number of arguments (not two)\n");
		return 1;
	}

	pipe_child = atoi(argv[1]);
	pipe_parent = atoi(argv[2]);
	/* PR_SET_NO_NEW_PRIVS already set by parent. */

	/* First step to test parent's layer1. */
	err = sync_with(pipe_child, pipe_parent);
	if (err)
		return err;

	/* Tries to send a signal, denied by layer1. */
	if (!kill(getppid(), 0)) {
		fprintf(stderr, "Successfully sent a signal to the parent");
		return 1;
	}

	/* Second step to test parent's layer1 and our layer2. */
	err = sync_with(pipe_child, pipe_parent);
	if (err)
		return err;

	ruleset_fd = landlock_create_ruleset(&layer2, sizeof(layer2), 0);
	if (ruleset_fd < 0) {
		perror("Failed to create the layer2 ruleset");
		return 1;
	}

	if (landlock_restrict_self(ruleset_fd, 0)) {
		perror("Failed to restrict self");
		return 1;
	}
	close(ruleset_fd);

	/* Tries to send a signal, denied by layer1. */
	if (!kill(getppid(), 0)) {
		fprintf(stderr, "Successfully sent a signal to the parent");
		return 1;
	}

	/* Tries to open ., denied by layer2. */
	if (open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC) >= 0) {
		fprintf(stderr, "Successfully opened /");
		return 1;
	}

	/* Third step to test our layer2 and layer3. */
	err = sync_with(pipe_child, pipe_parent);
	if (err)
		return err;

	ruleset_fd = landlock_create_ruleset(&layer3, sizeof(layer3), 0);
	if (ruleset_fd < 0) {
		perror("Failed to create the layer3 ruleset");
		return 1;
	}

	if (landlock_restrict_self(ruleset_fd, 0)) {
		perror("Failed to restrict self");
		return 1;
	}
	close(ruleset_fd);

	/* Tries to open ., denied by layer2. */
	if (open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC) >= 0) {
		fprintf(stderr, "Successfully opened /");
		return 1;
	}

	/* Tries to send a signal, denied by layer3. */
	if (!kill(getppid(), 0)) {
		fprintf(stderr, "Successfully sent a signal to the parent");
		return 1;
	}

	return 0;
}
