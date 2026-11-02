// SPDX-License-Identifier: GPL-2.0

/*
 * The |helper half of coredump_notify_signal_test. The kernel spawns this
 * with the coredump on stdin, so it cannot be part of the test binary.
 * It saves the dump and, once the notes have started, trips the fifo the
 * crashing task is polling so TIF_NOTIFY_SIGNAL is raised while the note
 * write is in flight.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "coredump_notify_signal.h"

int main(int argc, char *argv[])
{
	int fd_core_file;
	ssize_t ret;

	fd_core_file = open(NOTIFY_SIGNAL_CORE_TMPFILE,
			    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd_core_file < 0) {
		fprintf(stderr, "%s: open failed: %m\n", argv[0]);
		return EXIT_FAILURE;
	}

	ret = recv_coredump_notify_signal(STDIN_FILENO, fd_core_file, true);
	close(fd_core_file);
	if (ret < 0)
		goto err;

	/* The test polls for this name, so only create it once it is whole. */
	if (rename(NOTIFY_SIGNAL_CORE_TMPFILE, NOTIFY_SIGNAL_CORE_FILE)) {
		fprintf(stderr, "%s: rename failed: %m\n", argv[0]);
		goto err;
	}

	return EXIT_SUCCESS;

err:
	unlink(NOTIFY_SIGNAL_CORE_TMPFILE);
	return EXIT_FAILURE;
}
