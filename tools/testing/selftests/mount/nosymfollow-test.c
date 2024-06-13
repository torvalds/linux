// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>

#ifndef MS_NOSYMFOLLOW
# define MS_NOSYMFOLLOW 256     /* Do not follow symlinks */
#endif

#ifndef ST_NOSYMFOLLOW
# define ST_NOSYMFOLLOW 0x2000  /* Do not follow symlinks */
#endif

#define DATA "/tmp/data"
#define LINK "/tmp/symlink"
#define TMP  "/tmp"

static void die(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void vmaybe_write_file(bool enoent_ok, char *filename, char *fmt,
		va_list ap)
{
	ssize_t written;
	char buf[4096];
	int buf_len;
	int fd;

	buf_len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (buf_len < 0)
		die("vsnprintf failed: %s\n", strerror(errno));

	if (buf_len >= sizeof(buf))
		die("vsnprintf output truncated\n");

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		if ((errno == ENOENT) && enoent_ok)
			return;
		die("open of %s failed: %s\n", filename, strerror(errno));
	}

	written = write(fd, buf, buf_len);
	if (written != buf_len) {
		if (written >= 0) {
			die("short write to %s\n", filename);
		} else {
			die("write to %s failed: %s\n",
				filename, strerror(errno));
		}
	}

	if (close(fd) != 0)
		die("close of %s failed: %s\n", filename, strerror(errno));
}

static void maybe_write_file(char *filename, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmaybe_write_file(true, filename, fmt, ap);
	va_end(ap);
}

static void write_file(char *filename, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmaybe_write_file(false, filename, fmt, ap);
	va_end(ap);
}

static void create_and_enter_ns(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();

	if (unshare(CLONE_NEWUSER) != 0)
		die("unshare(CLONE_NEWUSER) failed: %s\n", strerror(errno));

	maybe_write_file("/proc/self/setgroups", "deny");
	write_file("/proc/self/uid_map", "0 %d 1", uid);
	write_file("/proc/self/gid_map", "0 %d 1", gid);

	if (setgid(0) != 0)
		die("setgid(0) failed %s\n", strerror(errno));
	if (setuid(0) != 0)
		die("setuid(0) failed %s\n", strerror(errno));

	if (unshare(CLONE_NEWNS) != 0)
		die("unshare(CLONE_NEWNS) failed: %s\n", strerror(errno));
}

static void setup_symlink(void)
{
	int data, err;

	data = creat(DATA, O_RDWR);
	if (data < 0)
		die("creat failed: %s\n", strerror(errno));

	err = symlink(DATA, LINK);
	if (err < 0)
		die("symlink failed: %s\n", strerror(errno));

	if (close(data) != 0)
		die("close of %s failed: %s\n", DATA, strerror(errno));
}

static void test_link_traversal(bool nosymfollow)
{
	int link;

	link = open(LINK, 0, O_RDWR);
	if (nosymfollow) {
		if ((link != -1 || errno != ELOOP)) {
			die("link traversal unexpected result: %d, %s\n",
					link, strerror(errno));
		}
	} else {
		if (link < 0)
			die("link traversal failed: %s\n", strerror(errno));

		if (close(link) != 0)
			die("close of link failed: %s\n", strerror(errno));
	}
}

static void test_readlink(void)
{
	char buf[4096];
	ssize_t ret;

	bzero(buf, sizeof(buf));

	ret = readlink(LINK, buf, sizeof(buf));
	if (ret < 0)
		die("readlink failed: %s\n", strerror(errno));
	if (strcmp(buf, DATA) != 0)
		die("readlink strcmp failed: '%s' '%s'\n", buf, DATA);
}

static void test_realpath(void)
{
	char *path = realpath(LINK, NULL);

	if (!path)
		die("realpath failed: %s\n", strerror(errno));
	if (strcmp(path, DATA) != 0)
		die("realpath strcmp failed\n");

	free(path);
}

static void test_statfs(bool nosymfollow)
{
	struct statfs buf;
	int ret;

	ret = statfs(TMP, &buf);
	if (ret)
		die("statfs failed: %s\n", strerror(errno));

	if (nosymfollow) {
		if ((buf.f_flags & ST_NOSYMFOLLOW) == 0)
			die("ST_NOSYMFOLLOW not set on %s\n", TMP);
	} else {
		if ((buf.f_flags & ST_NOSYMFOLLOW) != 0)
			die("ST_NOSYMFOLLOW set on %s\n", TMP);
	}
}

static void run_tests(bool nosymfollow)
{
	test_link_traversal(nosymfollow);
	test_readlink();
	test_realpath();
	test_statfs(nosymfollow);
}

int main(int argc, char **argv)
{
	create_and_enter_ns();

	if (mount("testing", TMP, "ramfs", 0, NULL) != 0)
		die("mount failed: %s\n", strerror(errno));

	setup_symlink();
	run_tests(false);

	if (mount("testing", TMP, "ramfs", MS_REMOUNT|MS_NOSYMFOLLOW, NULL) != 0)
		die("remount failed: %s\n", strerror(errno));

	run_tests(true);

	return EXIT_SUCCESS;
}
