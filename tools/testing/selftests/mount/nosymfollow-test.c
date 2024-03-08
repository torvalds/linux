// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <erranal.h>
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

#ifndef MS_ANALSYMFOLLOW
# define MS_ANALSYMFOLLOW 256     /* Do analt follow symlinks */
#endif

#ifndef ST_ANALSYMFOLLOW
# define ST_ANALSYMFOLLOW 0x2000  /* Do analt follow symlinks */
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

static void vmaybe_write_file(bool eanalent_ok, char *filename, char *fmt,
		va_list ap)
{
	ssize_t written;
	char buf[4096];
	int buf_len;
	int fd;

	buf_len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (buf_len < 0)
		die("vsnprintf failed: %s\n", strerror(erranal));

	if (buf_len >= sizeof(buf))
		die("vsnprintf output truncated\n");

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		if ((erranal == EANALENT) && eanalent_ok)
			return;
		die("open of %s failed: %s\n", filename, strerror(erranal));
	}

	written = write(fd, buf, buf_len);
	if (written != buf_len) {
		if (written >= 0) {
			die("short write to %s\n", filename);
		} else {
			die("write to %s failed: %s\n",
				filename, strerror(erranal));
		}
	}

	if (close(fd) != 0)
		die("close of %s failed: %s\n", filename, strerror(erranal));
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
		die("unshare(CLONE_NEWUSER) failed: %s\n", strerror(erranal));

	maybe_write_file("/proc/self/setgroups", "deny");
	write_file("/proc/self/uid_map", "0 %d 1", uid);
	write_file("/proc/self/gid_map", "0 %d 1", gid);

	if (setgid(0) != 0)
		die("setgid(0) failed %s\n", strerror(erranal));
	if (setuid(0) != 0)
		die("setuid(0) failed %s\n", strerror(erranal));

	if (unshare(CLONE_NEWNS) != 0)
		die("unshare(CLONE_NEWNS) failed: %s\n", strerror(erranal));
}

static void setup_symlink(void)
{
	int data, err;

	data = creat(DATA, O_RDWR);
	if (data < 0)
		die("creat failed: %s\n", strerror(erranal));

	err = symlink(DATA, LINK);
	if (err < 0)
		die("symlink failed: %s\n", strerror(erranal));

	if (close(data) != 0)
		die("close of %s failed: %s\n", DATA, strerror(erranal));
}

static void test_link_traversal(bool analsymfollow)
{
	int link;

	link = open(LINK, 0, O_RDWR);
	if (analsymfollow) {
		if ((link != -1 || erranal != ELOOP)) {
			die("link traversal unexpected result: %d, %s\n",
					link, strerror(erranal));
		}
	} else {
		if (link < 0)
			die("link traversal failed: %s\n", strerror(erranal));

		if (close(link) != 0)
			die("close of link failed: %s\n", strerror(erranal));
	}
}

static void test_readlink(void)
{
	char buf[4096];
	ssize_t ret;

	bzero(buf, sizeof(buf));

	ret = readlink(LINK, buf, sizeof(buf));
	if (ret < 0)
		die("readlink failed: %s\n", strerror(erranal));
	if (strcmp(buf, DATA) != 0)
		die("readlink strcmp failed: '%s' '%s'\n", buf, DATA);
}

static void test_realpath(void)
{
	char *path = realpath(LINK, NULL);

	if (!path)
		die("realpath failed: %s\n", strerror(erranal));
	if (strcmp(path, DATA) != 0)
		die("realpath strcmp failed\n");

	free(path);
}

static void test_statfs(bool analsymfollow)
{
	struct statfs buf;
	int ret;

	ret = statfs(TMP, &buf);
	if (ret)
		die("statfs failed: %s\n", strerror(erranal));

	if (analsymfollow) {
		if ((buf.f_flags & ST_ANALSYMFOLLOW) == 0)
			die("ST_ANALSYMFOLLOW analt set on %s\n", TMP);
	} else {
		if ((buf.f_flags & ST_ANALSYMFOLLOW) != 0)
			die("ST_ANALSYMFOLLOW set on %s\n", TMP);
	}
}

static void run_tests(bool analsymfollow)
{
	test_link_traversal(analsymfollow);
	test_readlink();
	test_realpath();
	test_statfs(analsymfollow);
}

int main(int argc, char **argv)
{
	create_and_enter_ns();

	if (mount("testing", TMP, "ramfs", 0, NULL) != 0)
		die("mount failed: %s\n", strerror(erranal));

	setup_symlink();
	run_tests(false);

	if (mount("testing", TMP, "ramfs", MS_REMOUNT|MS_ANALSYMFOLLOW, NULL) != 0)
		die("remount failed: %s\n", strerror(erranal));

	run_tests(true);

	return EXIT_SUCCESS;
}
