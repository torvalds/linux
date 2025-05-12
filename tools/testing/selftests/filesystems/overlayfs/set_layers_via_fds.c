// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__ // Use ll64

#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/mount.h>
#include <unistd.h>

#include "../../kselftest_harness.h"
#include "../../pidfd/pidfd.h"
#include "log.h"
#include "../utils.h"
#include "../wrappers.h"

FIXTURE(set_layers_via_fds) {
	int pidfd;
};

FIXTURE_SETUP(set_layers_via_fds)
{
	self->pidfd = -EBADF;
	EXPECT_EQ(mkdir("/set_layers_via_fds", 0755), 0);
	EXPECT_EQ(mkdir("/set_layers_via_fds_tmpfs", 0755), 0);
}

FIXTURE_TEARDOWN(set_layers_via_fds)
{
	if (self->pidfd >= 0) {
		EXPECT_EQ(sys_pidfd_send_signal(self->pidfd, SIGKILL, NULL, 0), 0);
		EXPECT_EQ(close(self->pidfd), 0);
	}
	umount2("/set_layers_via_fds", 0);
	EXPECT_EQ(rmdir("/set_layers_via_fds"), 0);

	umount2("/set_layers_via_fds_tmpfs", 0);
	EXPECT_EQ(rmdir("/set_layers_via_fds_tmpfs"), 0);
}

TEST_F(set_layers_via_fds, set_layers_via_fds)
{
	int fd_context, fd_tmpfs, fd_overlay;
	int layer_fds[] = { [0 ... 8] = -EBADF };
	bool layers_found[] = { [0 ... 8] =  false };
	size_t len = 0;
	char *line = NULL;
	FILE *f_mountinfo;

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);
	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_tmpfs, 0);
	ASSERT_EQ(close(fd_context), 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "w", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "u", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l1", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l2", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l3", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l4", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "d1", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "d2", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "d3", 0755), 0);

	layer_fds[0] = openat(fd_tmpfs, "w", O_DIRECTORY);
	ASSERT_GE(layer_fds[0], 0);

	layer_fds[1] = openat(fd_tmpfs, "u", O_DIRECTORY);
	ASSERT_GE(layer_fds[1], 0);

	layer_fds[2] = openat(fd_tmpfs, "l1", O_DIRECTORY);
	ASSERT_GE(layer_fds[2], 0);

	layer_fds[3] = openat(fd_tmpfs, "l2", O_DIRECTORY);
	ASSERT_GE(layer_fds[3], 0);

	layer_fds[4] = openat(fd_tmpfs, "l3", O_DIRECTORY);
	ASSERT_GE(layer_fds[4], 0);

	layer_fds[5] = openat(fd_tmpfs, "l4", O_DIRECTORY);
	ASSERT_GE(layer_fds[5], 0);

	layer_fds[6] = openat(fd_tmpfs, "d1", O_DIRECTORY);
	ASSERT_GE(layer_fds[6], 0);

	layer_fds[7] = openat(fd_tmpfs, "d2", O_DIRECTORY);
	ASSERT_GE(layer_fds[7], 0);

	layer_fds[8] = openat(fd_tmpfs, "d3", O_DIRECTORY);
	ASSERT_GE(layer_fds[8], 0);

	ASSERT_EQ(sys_move_mount(fd_tmpfs, "", -EBADF, "/tmp", MOVE_MOUNT_F_EMPTY_PATH), 0);
	ASSERT_EQ(close(fd_tmpfs), 0);

	fd_context = sys_fsopen("overlay", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_NE(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir", NULL, layer_fds[2]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "workdir",   NULL, layer_fds[0]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "upperdir",  NULL, layer_fds[1]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[2]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[3]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[4]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[5]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "datadir+",  NULL, layer_fds[6]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "datadir+",  NULL, layer_fds[7]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "datadir+",  NULL, layer_fds[8]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_STRING, "metacopy", "on", 0), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);

	fd_overlay = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_overlay, 0);

	ASSERT_EQ(sys_move_mount(fd_overlay, "", -EBADF, "/set_layers_via_fds", MOVE_MOUNT_F_EMPTY_PATH), 0);

	f_mountinfo = fopen("/proc/self/mountinfo", "r");
	ASSERT_NE(f_mountinfo, NULL);

	while (getline(&line, &len, f_mountinfo) != -1) {
		char *haystack = line;

		if (strstr(haystack, "workdir=/tmp/w"))
			layers_found[0] = true;
		if (strstr(haystack, "upperdir=/tmp/u"))
			layers_found[1] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l1"))
			layers_found[2] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l2"))
			layers_found[3] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l3"))
			layers_found[4] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l4"))
			layers_found[5] = true;
		if (strstr(haystack, "datadir+=/tmp/d1"))
			layers_found[6] = true;
		if (strstr(haystack, "datadir+=/tmp/d2"))
			layers_found[7] = true;
		if (strstr(haystack, "datadir+=/tmp/d3"))
			layers_found[8] = true;
	}
	free(line);

	for (int i = 0; i < ARRAY_SIZE(layer_fds); i++) {
		ASSERT_EQ(layers_found[i], true);
		ASSERT_EQ(close(layer_fds[i]), 0);
	}

	ASSERT_EQ(close(fd_context), 0);
	ASSERT_EQ(close(fd_overlay), 0);
	ASSERT_EQ(fclose(f_mountinfo), 0);
}

TEST_F(set_layers_via_fds, set_500_layers_via_fds)
{
	int fd_context, fd_tmpfs, fd_overlay, fd_work, fd_upper, fd_lower;
	int layer_fds[500] = { [0 ... 499] = -EBADF };

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);
	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_tmpfs, 0);
	ASSERT_EQ(close(fd_context), 0);

	for (int i = 0; i < ARRAY_SIZE(layer_fds); i++) {
		char path[100];

		sprintf(path, "l%d", i);
		ASSERT_EQ(mkdirat(fd_tmpfs, path, 0755), 0);
		layer_fds[i] = openat(fd_tmpfs, path, O_DIRECTORY);
		ASSERT_GE(layer_fds[i], 0);
	}

	ASSERT_EQ(mkdirat(fd_tmpfs, "w", 0755), 0);
	fd_work = openat(fd_tmpfs, "w", O_DIRECTORY);
	ASSERT_GE(fd_work, 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "u", 0755), 0);
	fd_upper = openat(fd_tmpfs, "u", O_DIRECTORY);
	ASSERT_GE(fd_upper, 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "l501", 0755), 0);
	fd_lower = openat(fd_tmpfs, "l501", O_DIRECTORY);
	ASSERT_GE(fd_lower, 0);

	ASSERT_EQ(sys_move_mount(fd_tmpfs, "", -EBADF, "/tmp", MOVE_MOUNT_F_EMPTY_PATH), 0);
	ASSERT_EQ(close(fd_tmpfs), 0);

	fd_context = sys_fsopen("overlay", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "workdir",   NULL, fd_work), 0);
	ASSERT_EQ(close(fd_work), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "upperdir",  NULL, fd_upper), 0);
	ASSERT_EQ(close(fd_upper), 0);

	for (int i = 0; i < ARRAY_SIZE(layer_fds); i++) {
		ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[i]), 0);
		ASSERT_EQ(close(layer_fds[i]), 0);
	}

	ASSERT_NE(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, fd_lower), 0);
	ASSERT_EQ(close(fd_lower), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);

	fd_overlay = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_overlay, 0);
	ASSERT_EQ(close(fd_context), 0);
	ASSERT_EQ(close(fd_overlay), 0);
}

TEST_F(set_layers_via_fds, set_override_creds)
{
	int fd_context, fd_tmpfs, fd_overlay;
	int layer_fds[] = { [0 ... 3] = -EBADF };
	pid_t pid;
	int pidfd;

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);
	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_tmpfs, 0);
	ASSERT_EQ(close(fd_context), 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "w", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "u", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l1", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l2", 0755), 0);

	layer_fds[0] = openat(fd_tmpfs, "w", O_DIRECTORY);
	ASSERT_GE(layer_fds[0], 0);

	layer_fds[1] = openat(fd_tmpfs, "u", O_DIRECTORY);
	ASSERT_GE(layer_fds[1], 0);

	layer_fds[2] = openat(fd_tmpfs, "l1", O_DIRECTORY);
	ASSERT_GE(layer_fds[2], 0);

	layer_fds[3] = openat(fd_tmpfs, "l2", O_DIRECTORY);
	ASSERT_GE(layer_fds[3], 0);

	ASSERT_EQ(sys_move_mount(fd_tmpfs, "", -EBADF, "/tmp", MOVE_MOUNT_F_EMPTY_PATH), 0);
	ASSERT_EQ(close(fd_tmpfs), 0);

	fd_context = sys_fsopen("overlay", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_NE(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir", NULL, layer_fds[2]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "workdir",   NULL, layer_fds[0]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "upperdir",  NULL, layer_fds[1]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[2]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[3]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_STRING, "metacopy", "on", 0), 0);

	pid = create_child(&pidfd, 0);
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		if (sys_fsconfig(fd_context, FSCONFIG_SET_FLAG, "override_creds", NULL, 0)) {
			TH_LOG("sys_fsconfig should have succeeded");
			_exit(EXIT_FAILURE);
		}

		_exit(EXIT_SUCCESS);
	}
	ASSERT_GE(sys_waitid(P_PID, pid, NULL, WEXITED), 0);
	ASSERT_GE(close(pidfd), 0);

	pid = create_child(&pidfd, 0);
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		if (sys_fsconfig(fd_context, FSCONFIG_SET_FLAG, "nooverride_creds", NULL, 0)) {
			TH_LOG("sys_fsconfig should have succeeded");
			_exit(EXIT_FAILURE);
		}

		_exit(EXIT_SUCCESS);
	}
	ASSERT_GE(sys_waitid(P_PID, pid, NULL, WEXITED), 0);
	ASSERT_GE(close(pidfd), 0);

	pid = create_child(&pidfd, 0);
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		if (sys_fsconfig(fd_context, FSCONFIG_SET_FLAG, "override_creds", NULL, 0)) {
			TH_LOG("sys_fsconfig should have succeeded");
			_exit(EXIT_FAILURE);
		}

		_exit(EXIT_SUCCESS);
	}
	ASSERT_GE(sys_waitid(P_PID, pid, NULL, WEXITED), 0);
	ASSERT_GE(close(pidfd), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);

	fd_overlay = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_overlay, 0);

	ASSERT_EQ(sys_move_mount(fd_overlay, "", -EBADF, "/set_layers_via_fds", MOVE_MOUNT_F_EMPTY_PATH), 0);

	ASSERT_EQ(close(fd_context), 0);
	ASSERT_EQ(close(fd_overlay), 0);
}

TEST_F(set_layers_via_fds, set_override_creds_invalid)
{
	int fd_context, fd_tmpfs, fd_overlay, ret;
	int layer_fds[] = { [0 ... 3] = -EBADF };
	pid_t pid;
	int fd_userns1, fd_userns2;
	int ipc_sockets[2];
	char c;
	const unsigned int predictable_fd_context_nr = 123;

	fd_userns1 = get_userns_fd(0, 0, 10000);
	ASSERT_GE(fd_userns1, 0);

	fd_userns2 = get_userns_fd(0, 1234, 10000);
	ASSERT_GE(fd_userns2, 0);

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets);
	ASSERT_GE(ret, 0);

	pid = create_child(&self->pidfd, 0);
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		if (close(ipc_sockets[0])) {
			TH_LOG("close should have succeeded");
			_exit(EXIT_FAILURE);
		}

		if (!switch_userns(fd_userns2, 0, 0, false)) {
			TH_LOG("switch_userns should have succeeded");
			_exit(EXIT_FAILURE);
		}

		if (read_nointr(ipc_sockets[1], &c, 1) != 1) {
			TH_LOG("read_nointr should have succeeded");
			_exit(EXIT_FAILURE);
		}

		if (close(ipc_sockets[1])) {
			TH_LOG("close should have succeeded");
			_exit(EXIT_FAILURE);
		}

		if (!sys_fsconfig(predictable_fd_context_nr, FSCONFIG_SET_FLAG, "override_creds", NULL, 0)) {
			TH_LOG("sys_fsconfig should have failed");
			_exit(EXIT_FAILURE);
		}

		_exit(EXIT_SUCCESS);
	}

	ASSERT_EQ(close(ipc_sockets[1]), 0);
	ASSERT_EQ(switch_userns(fd_userns1, 0, 0, false), true);
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);
	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_tmpfs, 0);
	ASSERT_EQ(close(fd_context), 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "w", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "u", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l1", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l2", 0755), 0);

	layer_fds[0] = openat(fd_tmpfs, "w", O_DIRECTORY);
	ASSERT_GE(layer_fds[0], 0);

	layer_fds[1] = openat(fd_tmpfs, "u", O_DIRECTORY);
	ASSERT_GE(layer_fds[1], 0);

	layer_fds[2] = openat(fd_tmpfs, "l1", O_DIRECTORY);
	ASSERT_GE(layer_fds[2], 0);

	layer_fds[3] = openat(fd_tmpfs, "l2", O_DIRECTORY);
	ASSERT_GE(layer_fds[3], 0);

	ASSERT_EQ(sys_move_mount(fd_tmpfs, "", -EBADF, "/tmp", MOVE_MOUNT_F_EMPTY_PATH), 0);
	ASSERT_EQ(close(fd_tmpfs), 0);

	fd_context = sys_fsopen("overlay", 0);
	ASSERT_GE(fd_context, 0);
	ASSERT_EQ(dup3(fd_context, predictable_fd_context_nr, 0), predictable_fd_context_nr);
	ASSERT_EQ(close(fd_context), 0);
	fd_context = predictable_fd_context_nr;
	ASSERT_EQ(write_nointr(ipc_sockets[0], "1", 1), 1);
	ASSERT_EQ(close(ipc_sockets[0]), 0);

	ASSERT_EQ(wait_for_pid(pid), 0);
	ASSERT_EQ(close(self->pidfd), 0);
	self->pidfd = -EBADF;

	ASSERT_NE(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir", NULL, layer_fds[2]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "workdir",   NULL, layer_fds[0]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "upperdir",  NULL, layer_fds[1]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[2]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[3]), 0);

	for (int i = 0; i < ARRAY_SIZE(layer_fds); i++)
		ASSERT_EQ(close(layer_fds[i]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FLAG, "userxattr", NULL, 0), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);

	fd_overlay = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_overlay, 0);

	ASSERT_EQ(sys_move_mount(fd_overlay, "", -EBADF, "/set_layers_via_fds", MOVE_MOUNT_F_EMPTY_PATH), 0);

	ASSERT_EQ(close(fd_context), 0);
	ASSERT_EQ(close(fd_overlay), 0);
	ASSERT_EQ(close(fd_userns1), 0);
	ASSERT_EQ(close(fd_userns2), 0);
}

TEST_F(set_layers_via_fds, set_override_creds_nomknod)
{
	int fd_context, fd_tmpfs, fd_overlay;
	int layer_fds[] = { [0 ... 3] = -EBADF };
	pid_t pid;
	int pidfd;

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);
	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_tmpfs, 0);
	ASSERT_EQ(close(fd_context), 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "w", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "u", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l1", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l2", 0755), 0);

	layer_fds[0] = openat(fd_tmpfs, "w", O_DIRECTORY);
	ASSERT_GE(layer_fds[0], 0);

	layer_fds[1] = openat(fd_tmpfs, "u", O_DIRECTORY);
	ASSERT_GE(layer_fds[1], 0);

	layer_fds[2] = openat(fd_tmpfs, "l1", O_DIRECTORY);
	ASSERT_GE(layer_fds[2], 0);

	layer_fds[3] = openat(fd_tmpfs, "l2", O_DIRECTORY);
	ASSERT_GE(layer_fds[3], 0);

	ASSERT_EQ(sys_move_mount(fd_tmpfs, "", -EBADF, "/tmp", MOVE_MOUNT_F_EMPTY_PATH), 0);
	ASSERT_EQ(close(fd_tmpfs), 0);

	fd_context = sys_fsopen("overlay", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_NE(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir", NULL, layer_fds[2]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "workdir",   NULL, layer_fds[0]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "upperdir",  NULL, layer_fds[1]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[2]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[3]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FLAG, "userxattr", NULL, 0), 0);

	pid = create_child(&pidfd, 0);
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		if (!cap_down(CAP_MKNOD))
			_exit(EXIT_FAILURE);

		if (!cap_down(CAP_SYS_ADMIN))
			_exit(EXIT_FAILURE);

		if (sys_fsconfig(fd_context, FSCONFIG_SET_FLAG, "override_creds", NULL, 0))
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}
	ASSERT_EQ(sys_waitid(P_PID, pid, NULL, WEXITED), 0);
	ASSERT_GE(close(pidfd), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);

	fd_overlay = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_overlay, 0);

	ASSERT_EQ(sys_move_mount(fd_overlay, "", -EBADF, "/set_layers_via_fds", MOVE_MOUNT_F_EMPTY_PATH), 0);
	ASSERT_EQ(mknodat(fd_overlay, "dev-zero", S_IFCHR | 0644, makedev(1, 5)), -1);
	ASSERT_EQ(errno, EPERM);

	ASSERT_EQ(close(fd_context), 0);
	ASSERT_EQ(close(fd_overlay), 0);
}

TEST_F(set_layers_via_fds, set_500_layers_via_opath_fds)
{
	int fd_context, fd_tmpfs, fd_overlay, fd_work, fd_upper, fd_lower;
	int layer_fds[500] = { [0 ... 499] = -EBADF };

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);
	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_tmpfs, 0);
	ASSERT_EQ(close(fd_context), 0);

	for (int i = 0; i < ARRAY_SIZE(layer_fds); i++) {
		char path[100];

		sprintf(path, "l%d", i);
		ASSERT_EQ(mkdirat(fd_tmpfs, path, 0755), 0);
		layer_fds[i] = openat(fd_tmpfs, path, O_DIRECTORY | O_PATH);
		ASSERT_GE(layer_fds[i], 0);
	}

	ASSERT_EQ(mkdirat(fd_tmpfs, "w", 0755), 0);
	fd_work = openat(fd_tmpfs, "w", O_DIRECTORY | O_PATH);
	ASSERT_GE(fd_work, 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "u", 0755), 0);
	fd_upper = openat(fd_tmpfs, "u", O_DIRECTORY | O_PATH);
	ASSERT_GE(fd_upper, 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "l501", 0755), 0);
	fd_lower = openat(fd_tmpfs, "l501", O_DIRECTORY | O_PATH);
	ASSERT_GE(fd_lower, 0);

	ASSERT_EQ(sys_move_mount(fd_tmpfs, "", -EBADF, "/tmp", MOVE_MOUNT_F_EMPTY_PATH), 0);
	ASSERT_EQ(close(fd_tmpfs), 0);

	fd_context = sys_fsopen("overlay", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "workdir",   NULL, fd_work), 0);
	ASSERT_EQ(close(fd_work), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "upperdir",  NULL, fd_upper), 0);
	ASSERT_EQ(close(fd_upper), 0);

	for (int i = 0; i < ARRAY_SIZE(layer_fds); i++) {
		ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[i]), 0);
		ASSERT_EQ(close(layer_fds[i]), 0);
	}

	ASSERT_NE(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, fd_lower), 0);
	ASSERT_EQ(close(fd_lower), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);

	fd_overlay = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_overlay, 0);
	ASSERT_EQ(close(fd_context), 0);
	ASSERT_EQ(close(fd_overlay), 0);
}

TEST_F(set_layers_via_fds, set_layers_via_detached_mount_fds)
{
	int fd_context, fd_tmpfs, fd_overlay, fd_tmp;
	int layer_fds[] = { [0 ... 8] = -EBADF };
	bool layers_found[] = { [0 ... 8] =  false };
	size_t len = 0;
	char *line = NULL;
	FILE *f_mountinfo;

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	fd_context = sys_fsopen("tmpfs", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);
	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_tmpfs, 0);
	ASSERT_EQ(close(fd_context), 0);

	ASSERT_EQ(mkdirat(fd_tmpfs, "u", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "u/upper", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "u/work", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l1", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l2", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l3", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "l4", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "d1", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "d2", 0755), 0);
	ASSERT_EQ(mkdirat(fd_tmpfs, "d3", 0755), 0);

	ASSERT_EQ(sys_move_mount(fd_tmpfs, "", -EBADF, "/set_layers_via_fds_tmpfs", MOVE_MOUNT_F_EMPTY_PATH), 0);

	fd_tmp = open_tree(fd_tmpfs, "u", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(fd_tmp, 0);

	layer_fds[0] = openat(fd_tmp, "upper", O_CLOEXEC | O_DIRECTORY | O_PATH);
	ASSERT_GE(layer_fds[0], 0);

	layer_fds[1] = openat(fd_tmp, "work", O_CLOEXEC | O_DIRECTORY | O_PATH);
	ASSERT_GE(layer_fds[1], 0);

	layer_fds[2] = open_tree(fd_tmpfs, "l1", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(layer_fds[2], 0);

	layer_fds[3] = open_tree(fd_tmpfs, "l2", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(layer_fds[3], 0);

	layer_fds[4] = open_tree(fd_tmpfs, "l3", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(layer_fds[4], 0);

	layer_fds[5] = open_tree(fd_tmpfs, "l4", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(layer_fds[5], 0);

	layer_fds[6] = open_tree(fd_tmpfs, "d1", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(layer_fds[6], 0);

	layer_fds[7] = open_tree(fd_tmpfs, "d2", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(layer_fds[7], 0);

	layer_fds[8] = open_tree(fd_tmpfs, "d3", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(layer_fds[8], 0);

	ASSERT_EQ(close(fd_tmpfs), 0);

	fd_context = sys_fsopen("overlay", 0);
	ASSERT_GE(fd_context, 0);

	ASSERT_NE(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir", NULL, layer_fds[2]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "upperdir",  NULL, layer_fds[0]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "workdir",   NULL, layer_fds[1]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[2]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[3]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[4]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "lowerdir+", NULL, layer_fds[5]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "datadir+",  NULL, layer_fds[6]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "datadir+",  NULL, layer_fds[7]), 0);
	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_FD, "datadir+",  NULL, layer_fds[8]), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_SET_STRING, "metacopy", "on", 0), 0);

	ASSERT_EQ(sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0), 0);

	fd_overlay = sys_fsmount(fd_context, 0, 0);
	ASSERT_GE(fd_overlay, 0);

	ASSERT_EQ(sys_move_mount(fd_overlay, "", -EBADF, "/set_layers_via_fds", MOVE_MOUNT_F_EMPTY_PATH), 0);

	f_mountinfo = fopen("/proc/self/mountinfo", "r");
	ASSERT_NE(f_mountinfo, NULL);

	while (getline(&line, &len, f_mountinfo) != -1) {
		char *haystack = line;

		if (strstr(haystack, "workdir=/tmp/w"))
			layers_found[0] = true;
		if (strstr(haystack, "upperdir=/tmp/u"))
			layers_found[1] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l1"))
			layers_found[2] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l2"))
			layers_found[3] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l3"))
			layers_found[4] = true;
		if (strstr(haystack, "lowerdir+=/tmp/l4"))
			layers_found[5] = true;
		if (strstr(haystack, "datadir+=/tmp/d1"))
			layers_found[6] = true;
		if (strstr(haystack, "datadir+=/tmp/d2"))
			layers_found[7] = true;
		if (strstr(haystack, "datadir+=/tmp/d3"))
			layers_found[8] = true;
	}
	free(line);

	for (int i = 0; i < ARRAY_SIZE(layer_fds); i++) {
		ASSERT_EQ(layers_found[i], true);
		ASSERT_EQ(close(layer_fds[i]), 0);
	}

	ASSERT_EQ(close(fd_context), 0);
	ASSERT_EQ(close(fd_overlay), 0);
	ASSERT_EQ(fclose(f_mountinfo), 0);
}

TEST_HARNESS_MAIN
