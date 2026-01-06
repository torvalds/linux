/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(__NET_KSFT_H__)
#define __NET_KSFT_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static inline void ksft_ready(void)
{
	const char msg[7] = "ready\n";
	char *env_str;
	int fd;

	env_str = getenv("KSFT_READY_FD");
	if (env_str) {
		fd = atoi(env_str);
		if (!fd) {
			fprintf(stderr, "invalid KSFT_READY_FD = '%s'\n",
				env_str);
			return;
		}
	} else {
		fd = STDOUT_FILENO;
	}

	if (write(fd, msg, sizeof(msg)) < 0)
		perror("write()");
	if (fd != STDOUT_FILENO)
		close(fd);
}

static inline void ksft_wait(void)
{
	char *env_str;
	char byte;
	int fd;

	env_str = getenv("KSFT_WAIT_FD");
	if (env_str) {
		fd = atoi(env_str);
		if (!fd) {
			fprintf(stderr, "invalid KSFT_WAIT_FD = '%s'\n",
				env_str);
			return;
		}
	} else {
		/* Not running in KSFT env, wait for input from STDIN instead */
		fd = STDIN_FILENO;
	}

	if (read(fd, &byte, sizeof(byte)) < 0)
		perror("read()");
	if (fd != STDIN_FILENO)
		close(fd);
}

#endif
