/*
 * Copyright © 2018 Alexey Dobriyan <adobriyan@gmail.com>
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
/* Test that /proc/loadavg correctly reports last pid in pid namespace. */
#include <errno.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
	pid_t pid;
	int wstatus;

	if (unshare(CLONE_NEWPID) == -1) {
		if (errno == ENOSYS || errno == EPERM)
			return 4;
		return 1;
	}

	pid = fork();
	if (pid == -1)
		return 1;
	if (pid == 0) {
		char buf[128], *p;
		int fd;
		ssize_t rv;

		fd = open("/proc/loadavg" , O_RDONLY);
		if (fd == -1)
			return 1;
		rv = read(fd, buf, sizeof(buf));
		if (rv < 3)
			return 1;
		p = buf + rv;

		/* pid 1 */
		if (!(p[-3] == ' ' && p[-2] == '1' && p[-1] == '\n'))
			return 1;

		pid = fork();
		if (pid == -1)
			return 1;
		if (pid == 0)
			return 0;
		if (waitpid(pid, NULL, 0) == -1)
			return 1;

		lseek(fd, 0, SEEK_SET);
		rv = read(fd, buf, sizeof(buf));
		if (rv < 3)
			return 1;
		p = buf + rv;

		/* pid 2 */
		if (!(p[-3] == ' ' && p[-2] == '2' && p[-1] == '\n'))
			return 1;

		return 0;
	}

	if (waitpid(pid, &wstatus, 0) == -1)
		return 1;
	if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0)
		return 0;
	return 1;
}
