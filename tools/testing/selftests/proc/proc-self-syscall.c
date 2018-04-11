#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static inline ssize_t sys_read(int fd, void *buf, size_t len)
{
	return syscall(SYS_read, fd, buf, len);
}

int main(void)
{
	char buf1[64];
	char buf2[64];
	int fd;
	ssize_t rv;

	fd = open("/proc/self/syscall", O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT)
			return 2;
		return 1;
	}

	/* Do direct system call as libc can wrap anything. */
	snprintf(buf1, sizeof(buf1), "%ld 0x%lx 0x%lx 0x%lx",
		 (long)SYS_read, (long)fd, (long)buf2, (long)sizeof(buf2));

	memset(buf2, 0, sizeof(buf2));
	rv = sys_read(fd, buf2, sizeof(buf2));
	if (rv < 0)
		return 1;
	if (rv < strlen(buf1))
		return 1;
	if (strncmp(buf1, buf2, strlen(buf1)) != 0)
		return 1;

	return 0;
}
