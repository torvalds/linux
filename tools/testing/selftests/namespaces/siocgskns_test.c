// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/nsfs.h>
#include <arpa/inet.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"
#include "wrappers.h"

#ifndef SIOCGSKNS
#define SIOCGSKNS 0x894C
#endif

#ifndef FD_NSFS_ROOT
#define FD_NSFS_ROOT -10003
#endif

#ifndef FILEID_NSFS
#define FILEID_NSFS 0xf1
#endif

/*
 * Test basic SIOCGSKNS functionality.
 * Create a socket and verify SIOCGSKNS returns the correct network namespace.
 */
TEST(siocgskns_basic)
{
	int sock_fd, netns_fd, current_netns_fd;
	struct stat st1, st2;

	/* Create a TCP socket */
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(sock_fd, 0);

	/* Use SIOCGSKNS to get network namespace */
	netns_fd = ioctl(sock_fd, SIOCGSKNS);
	if (netns_fd < 0) {
		close(sock_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_fd, 0);
	}

	/* Get current network namespace */
	current_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(current_netns_fd, 0);

	/* Verify they match */
	ASSERT_EQ(fstat(netns_fd, &st1), 0);
	ASSERT_EQ(fstat(current_netns_fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);

	close(sock_fd);
	close(netns_fd);
	close(current_netns_fd);
}

/*
 * Test that socket file descriptors keep network namespaces active.
 * Create a network namespace, create a socket in it, then exit the namespace.
 * The namespace should remain active while the socket FD is held.
 */
TEST(siocgskns_keeps_netns_active)
{
	int sock_fd, netns_fd, test_fd;
	int ipc_sockets[2];
	pid_t pid;
	int status;
	struct stat st;

	EXPECT_EQ(socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child: create new netns and socket */
		close(ipc_sockets[0]);

		if (unshare(CLONE_NEWNET) < 0) {
			TH_LOG("unshare(CLONE_NEWNET) failed: %s", strerror(errno));
			close(ipc_sockets[1]);
			exit(1);
		}

		/* Create a socket in the new network namespace */
		sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock_fd < 0) {
			TH_LOG("socket() failed: %s", strerror(errno));
			close(ipc_sockets[1]);
			exit(1);
		}

		/* Send socket FD to parent via SCM_RIGHTS */
		struct msghdr msg = {0};
		struct iovec iov = {0};
		char buf[1] = {'X'};
		char cmsg_buf[CMSG_SPACE(sizeof(int))];

		iov.iov_base = buf;
		iov.iov_len = 1;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = cmsg_buf;
		msg.msg_controllen = sizeof(cmsg_buf);

		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cmsg), &sock_fd, sizeof(int));

		if (sendmsg(ipc_sockets[1], &msg, 0) < 0) {
			close(sock_fd);
			close(ipc_sockets[1]);
			exit(1);
		}

		close(sock_fd);
		close(ipc_sockets[1]);
		exit(0);
	}

	/* Parent: receive socket FD */
	close(ipc_sockets[1]);

	struct msghdr msg = {0};
	struct iovec iov = {0};
	char buf[1];
	char cmsg_buf[CMSG_SPACE(sizeof(int))];

	iov.iov_base = buf;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);

	ssize_t n = recvmsg(ipc_sockets[0], &msg, 0);
	close(ipc_sockets[0]);
	ASSERT_EQ(n, 1);

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	ASSERT_NE(cmsg, NULL);
	ASSERT_EQ(cmsg->cmsg_type, SCM_RIGHTS);

	memcpy(&sock_fd, CMSG_DATA(cmsg), sizeof(int));

	/* Wait for child to exit */
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	if (WEXITSTATUS(status) != 0)
		SKIP(close(sock_fd); return, "Child failed to create namespace");

	/* Get network namespace from socket */
	netns_fd = ioctl(sock_fd, SIOCGSKNS);
	if (netns_fd < 0) {
		close(sock_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_fd, 0);
	}

	ASSERT_EQ(fstat(netns_fd, &st), 0);

	/*
	 * Namespace should still be active because socket FD keeps it alive.
	 * Try to access it via /proc/self/fd/<fd>.
	 */
	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", netns_fd);
	test_fd = open(path, O_RDONLY);
	ASSERT_GE(test_fd, 0);
	close(test_fd);
	close(netns_fd);

	/* Close socket - namespace should become inactive */
	close(sock_fd);

	/* Try SIOCGSKNS again - should fail since socket is closed */
	ASSERT_LT(ioctl(sock_fd, SIOCGSKNS), 0);
}

TEST_HARNESS_MAIN
