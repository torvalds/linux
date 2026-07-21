/* SPDX-License-Identifier: GPL-2.0 */
/* Helpers shared by the binfmt_misc selftests. */
#ifndef __SELFTESTS_EXEC_BINFMT_MISC_COMMON_H
#define __SELFTESTS_EXEC_BINFMT_MISC_COMMON_H

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#define BINFMT_DIR	"/proc/sys/fs/binfmt_misc"
#define BINFMT_REG	BINFMT_DIR "/register"

static inline int copy_file(const char *src, const char *dst)
{
	char buf[4096];
	int in, out;
	ssize_t n;

	in = open(src, O_RDONLY);
	if (in < 0)
		return -1;
	/* The tests share /tmp, so never write through a name they don't own. */
	unlink(dst);
	out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0755);
	if (out < 0) {
		close(in);
		return -1;
	}
	while ((n = read(in, buf, sizeof(buf))) > 0) {
		if (write(out, buf, n) != n) {
			close(in);
			close(out);
			return -1;
		}
	}
	close(in);
	close(out);
	return n < 0 ? -1 : 0;
}

/* Write @rule to the register file, preserving the write's errno. */
static inline int write_reg(const char *rule)
{
	int fd, saved;
	ssize_t n;

	fd = open(BINFMT_REG, O_WRONLY);
	if (fd < 0)
		return -1;
	n = write(fd, rule, strlen(rule));
	saved = errno;
	close(fd);
	errno = saved;
	return n < 0 ? -1 : 0;
}

static inline void unregister(const char *name)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, sizeof(path), BINFMT_DIR "/%s", name);
	fd = open(path, O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "-1", 2) < 0)
			; /* best effort */
		close(fd);
	}
}

/* Mount binfmt_misc unless it already is, and report whether it is usable. */
static inline bool binfmt_misc_available(void)
{
	if (access(BINFMT_REG, F_OK) < 0)
		mount("binfmt_misc", BINFMT_DIR, "binfmt_misc", 0, NULL);
	return access(BINFMT_REG, F_OK) == 0;
}

/* Absolute path of @name in the directory this test was built into. */
static inline int artifact_path(char *out, size_t sz, const char *name)
{
	char exe[PATH_MAX];
	ssize_t n;

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n < 0)
		return -1;
	exe[n] = '\0';
	if ((size_t)snprintf(out, sz, "%s/%s", dirname(exe), name) >= sz)
		return -1;
	return 0;
}

#endif /* __SELFTESTS_EXEC_BINFMT_MISC_COMMON_H */
