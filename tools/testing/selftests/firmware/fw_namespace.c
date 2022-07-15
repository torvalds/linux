// SPDX-License-Identifier: GPL-2.0
/* Test triggering of loading of firmware from different mount
 * namespaces. Expect firmware to be always loaded from the mount
 * namespace of PID 1. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CLONE_NEWNS
# define CLONE_NEWNS 0x00020000
#endif

static char *fw_path = NULL;

static void die(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (fw_path)
		unlink(fw_path);
	umount("/lib/firmware");
	exit(EXIT_FAILURE);
}

static void trigger_fw(const char *fw_name, const char *sys_path)
{
	int fd;

	fd = open(sys_path, O_WRONLY);
	if (fd < 0)
		die("open failed: %s\n",
		    strerror(errno));
	if (write(fd, fw_name, strlen(fw_name)) != strlen(fw_name))
		exit(EXIT_FAILURE);
	close(fd);
}

static void setup_fw(const char *fw_path)
{
	int fd;
	const char fw[] = "ABCD0123";

	fd = open(fw_path, O_WRONLY | O_CREAT, 0600);
	if (fd < 0)
		die("open failed: %s\n",
		    strerror(errno));
	if (write(fd, fw, sizeof(fw) -1) != sizeof(fw) -1)
		die("write failed: %s\n",
		    strerror(errno));
	close(fd);
}

static bool test_fw_in_ns(const char *fw_name, const char *sys_path, bool block_fw_in_parent_ns)
{
	pid_t child;

	if (block_fw_in_parent_ns)
		if (mount("test", "/lib/firmware", "tmpfs", MS_RDONLY, NULL) == -1)
			die("blocking firmware in parent ns failed\n");

	child = fork();
	if (child == -1) {
		die("fork failed: %s\n",
			strerror(errno));
	}
	if (child != 0) { /* parent */
		pid_t pid;
		int status;

		pid = waitpid(child, &status, 0);
		if (pid == -1) {
			die("waitpid failed: %s\n",
				strerror(errno));
		}
		if (pid != child) {
			die("waited for %d got %d\n",
				child, pid);
		}
		if (!WIFEXITED(status)) {
			die("child did not terminate cleanly\n");
		}
		if (block_fw_in_parent_ns)
			umount("/lib/firmware");
		return WEXITSTATUS(status) == EXIT_SUCCESS ? true : false;
	}

	if (unshare(CLONE_NEWNS) != 0) {
		die("unshare(CLONE_NEWNS) failed: %s\n",
			strerror(errno));
	}
	if (mount(NULL, "/", NULL, MS_SLAVE|MS_REC, NULL) == -1)
		die("remount root in child ns failed\n");

	if (!block_fw_in_parent_ns) {
		if (mount("test", "/lib/firmware", "tmpfs", MS_RDONLY, NULL) == -1)
			die("blocking firmware in child ns failed\n");
	} else
		umount("/lib/firmware");

	trigger_fw(fw_name, sys_path);

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	const char *fw_name = "test-firmware.bin";
	char *sys_path;
	if (argc != 2)
		die("usage: %s sys_path\n", argv[0]);

	/* Mount tmpfs to /lib/firmware so we don't have to assume
	   that it is writable for us.*/
	if (mount("test", "/lib/firmware", "tmpfs", 0, NULL) == -1)
		die("mounting tmpfs to /lib/firmware failed\n");

	sys_path = argv[1];
	if (asprintf(&fw_path, "/lib/firmware/%s", fw_name) < 0)
		die("error: failed to build full fw_path\n");

	setup_fw(fw_path);

	setvbuf(stdout, NULL, _IONBF, 0);
	/* Positive case: firmware in PID1 mount namespace */
	printf("Testing with firmware in parent namespace (assumed to be same file system as PID1)\n");
	if (!test_fw_in_ns(fw_name, sys_path, false))
		die("error: failed to access firmware\n");

	/* Negative case: firmware in child mount namespace, expected to fail */
	printf("Testing with firmware in child namespace\n");
	if (test_fw_in_ns(fw_name, sys_path, true))
		die("error: firmware access did not fail\n");

	unlink(fw_path);
	free(fw_path);
	umount("/lib/firmware");
	exit(EXIT_SUCCESS);
}
