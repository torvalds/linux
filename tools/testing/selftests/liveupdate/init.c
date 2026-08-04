// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#include <fcntl.h>
#include <linux/kexec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define COMMAND_LINE_SIZE 2048
#define KERNEL_IMAGE "/kernel"
#define INITRD_IMAGE "/initrd.img"
#define TEST_BINARY "/test_binary"

static int mount_filesystems(void)
{
	if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0) {
		fprintf(stderr, "INIT: Warning: Failed to mount devtmpfs\n");
		return -1;
	}

	if (mount("debugfs", "/debugfs", "debugfs", 0, NULL) < 0) {
		fprintf(stderr, "INIT: Failed to mount debugfs\n");
		return -1;
	}

	if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
		fprintf(stderr, "INIT: Failed to mount proc\n");
		return -1;
	}

	return 0;
}

static long kexec_file_load(int kernel_fd, int initrd_fd,
			    unsigned long cmdline_len, const char *cmdline,
			    unsigned long flags)
{
	return syscall(__NR_kexec_file_load, kernel_fd, initrd_fd, cmdline_len,
		       cmdline, flags);
}

static int kexec_load(void)
{
	char cmdline[COMMAND_LINE_SIZE];
	int kernel_fd, initrd_fd, err;
	ssize_t len;
	int fd;

	fd = open("/proc/cmdline", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "INIT: Failed to read /proc/cmdline\n");

		return -1;
	}

	len = read(fd, cmdline, sizeof(cmdline) - 1);
	close(fd);
	if (len < 0)
		return -1;

	cmdline[len] = 0;
	if (len > 0 && cmdline[len - 1] == '\n')
		cmdline[len - 1] = 0;

	strncat(cmdline, " luo_stage=2", sizeof(cmdline) - strlen(cmdline) - 1);

	kernel_fd = open(KERNEL_IMAGE, O_RDONLY);
	if (kernel_fd < 0) {
		fprintf(stderr, "INIT: Failed to open kernel image\n");
		return -1;
	}

	initrd_fd = open(INITRD_IMAGE, O_RDONLY);
	if (initrd_fd < 0) {
		fprintf(stderr, "INIT: Failed to open initrd image\n");
		close(kernel_fd);
		return -1;
	}

	err = kexec_file_load(kernel_fd, initrd_fd, strlen(cmdline) + 1,
			      cmdline, 0);

	close(initrd_fd);
	close(kernel_fd);

	return err;
}

static int run_test(int stage)
{
	char stage_arg[32];
	int status;
	pid_t pid;

	snprintf(stage_arg, sizeof(stage_arg), "%d", stage);

	pid = fork();
	if (pid < 0)
		return -1;

	if (!pid) {
		char *const argv[] = {TEST_BINARY, "-s", stage_arg, NULL};

		execve(TEST_BINARY, argv, NULL);
		fprintf(stderr, "INIT: execve failed\n");
		_exit(1);
	}

	waitpid(pid, &status, 0);

	return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static int get_current_stage(void)
{
	char cmdline[COMMAND_LINE_SIZE];
	ssize_t len;
	int fd;

	fd = open("/proc/cmdline", O_RDONLY);
	if (fd < 0)
		return -1;

	len = read(fd, cmdline, sizeof(cmdline) - 1);
	close(fd);

	if (len < 0)
		return -1;

	cmdline[len] = 0;

	return strstr(cmdline, "luo_stage=2") ? 2 : 1;
}

int main(int argc, char *argv[])
{
	int current_stage;
	int err;

	if (mount_filesystems())
		goto err_reboot;

	current_stage = get_current_stage();
	if (current_stage < 0) {
		fprintf(stderr, "INIT: Failed to read cmdline");
		goto err_reboot;
	}

	printf("INIT: Starting Stage %d\n", current_stage);

	if (current_stage == 1 && kexec_load()) {
		fprintf(stderr, "INIT: Failed to load kexec kernel\n");
		goto err_reboot;
	}

	if (run_test(current_stage)) {
		fprintf(stderr, "INIT: Test binary returned failure\n");
		goto err_reboot;
	}

	printf("INIT: Stage %d completed successfully.\n", current_stage);
	reboot(current_stage == 1 ? RB_KEXEC : RB_AUTOBOOT);

	return 0;

err_reboot:
	reboot(RB_AUTOBOOT);

	return -1;
}
