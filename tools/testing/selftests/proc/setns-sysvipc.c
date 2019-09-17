/*
 * Copyright © 2019 Alexey Dobriyan <adobriyan@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Test that setns(CLONE_NEWIPC) points to new /proc/sysvipc content even
 * if old one is in dcache.
 */
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>

static pid_t pid = -1;

static void f(void)
{
	if (pid > 0) {
		kill(pid, SIGTERM);
	}
}

int main(void)
{
	int fd[2];
	char _ = 0;
	int nsfd;

	atexit(f);

	/* Check for priviledges and syscall availability straight away. */
	if (unshare(CLONE_NEWIPC) == -1) {
		if (errno == ENOSYS || errno == EPERM) {
			return 4;
		}
		return 1;
	}
	/* Distinguisher between two otherwise empty IPC namespaces. */
	if (shmget(IPC_PRIVATE, 1, IPC_CREAT) == -1) {
		return 1;
	}

	if (pipe(fd) == -1) {
		return 1;
	}

	pid = fork();
	if (pid == -1) {
		return 1;
	}

	if (pid == 0) {
		if (unshare(CLONE_NEWIPC) == -1) {
			return 1;
		}

		if (write(fd[1], &_, 1) != 1) {
			return 1;
		}

		pause();

		return 0;
	}

	if (read(fd[0], &_, 1) != 1) {
		return 1;
	}

	{
		char buf[64];
		snprintf(buf, sizeof(buf), "/proc/%u/ns/ipc", pid);
		nsfd = open(buf, O_RDONLY);
		if (nsfd == -1) {
			return 1;
		}
	}

	/* Reliably pin dentry into dcache. */
	(void)open("/proc/sysvipc/shm", O_RDONLY);

	if (setns(nsfd, CLONE_NEWIPC) == -1) {
		return 1;
	}

	kill(pid, SIGTERM);
	pid = 0;

	{
		char buf[4096];
		ssize_t rv;
		int fd;

		fd = open("/proc/sysvipc/shm", O_RDONLY);
		if (fd == -1) {
			return 1;
		}

#define S32 "       key      shmid perms       size  cpid  lpid nattch   uid   gid  cuid  cgid      atime      dtime      ctime        rss       swap\n"
#define S64 "       key      shmid perms                  size  cpid  lpid nattch   uid   gid  cuid  cgid      atime      dtime      ctime                   rss                  swap\n"
		rv = read(fd, buf, sizeof(buf));
		if (rv == strlen(S32)) {
			assert(memcmp(buf, S32, strlen(S32)) == 0);
		} else if (rv == strlen(S64)) {
			assert(memcmp(buf, S64, strlen(S64)) == 0);
		} else {
			assert(0);
		}
	}

	return 0;
}
