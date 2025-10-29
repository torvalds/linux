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
	ASSERT_EQ(WEXITSTATUS(status), 0);

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

/*
 * Test SIOCGSKNS with different socket types (TCP, UDP, RAW).
 */
TEST(siocgskns_socket_types)
{
	int sock_tcp, sock_udp, sock_raw;
	int netns_tcp, netns_udp, netns_raw;
	struct stat st_tcp, st_udp, st_raw;

	/* TCP socket */
	sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(sock_tcp, 0);

	/* UDP socket */
	sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
	ASSERT_GE(sock_udp, 0);

	/* RAW socket (may require privileges) */
	sock_raw = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sock_raw < 0 && (errno == EPERM || errno == EACCES)) {
		sock_raw = -1; /* Skip raw socket test */
	}

	/* Test SIOCGSKNS on TCP */
	netns_tcp = ioctl(sock_tcp, SIOCGSKNS);
	if (netns_tcp < 0) {
		close(sock_tcp);
		close(sock_udp);
		if (sock_raw >= 0) close(sock_raw);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_tcp, 0);
	}

	/* Test SIOCGSKNS on UDP */
	netns_udp = ioctl(sock_udp, SIOCGSKNS);
	ASSERT_GE(netns_udp, 0);

	/* Test SIOCGSKNS on RAW (if available) */
	if (sock_raw >= 0) {
		netns_raw = ioctl(sock_raw, SIOCGSKNS);
		ASSERT_GE(netns_raw, 0);
	}

	/* Verify all return the same network namespace */
	ASSERT_EQ(fstat(netns_tcp, &st_tcp), 0);
	ASSERT_EQ(fstat(netns_udp, &st_udp), 0);
	ASSERT_EQ(st_tcp.st_ino, st_udp.st_ino);

	if (sock_raw >= 0) {
		ASSERT_EQ(fstat(netns_raw, &st_raw), 0);
		ASSERT_EQ(st_tcp.st_ino, st_raw.st_ino);
		close(netns_raw);
		close(sock_raw);
	}

	close(netns_tcp);
	close(netns_udp);
	close(sock_tcp);
	close(sock_udp);
}

/*
 * Test SIOCGSKNS across setns.
 * Create a socket in netns A, switch to netns B, verify SIOCGSKNS still
 * returns netns A.
 */
TEST(siocgskns_across_setns)
{
	int sock_fd, netns_a_fd, netns_b_fd, result_fd;
	struct stat st_a;

	/* Get current netns (A) */
	netns_a_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(netns_a_fd, 0);
	ASSERT_EQ(fstat(netns_a_fd, &st_a), 0);

	/* Create socket in netns A */
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(sock_fd, 0);

	/* Create new netns (B) */
	ASSERT_EQ(unshare(CLONE_NEWNET), 0);

	netns_b_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(netns_b_fd, 0);

	/* Get netns from socket created in A */
	result_fd = ioctl(sock_fd, SIOCGSKNS);
	if (result_fd < 0) {
		close(sock_fd);
		setns(netns_a_fd, CLONE_NEWNET);
		close(netns_a_fd);
		close(netns_b_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(result_fd, 0);
	}

	/* Verify it still points to netns A */
	struct stat st_result_stat;
	ASSERT_EQ(fstat(result_fd, &st_result_stat), 0);
	ASSERT_EQ(st_a.st_ino, st_result_stat.st_ino);

	close(result_fd);
	close(sock_fd);
	close(netns_b_fd);

	/* Restore original netns */
	ASSERT_EQ(setns(netns_a_fd, CLONE_NEWNET), 0);
	close(netns_a_fd);
}

/*
 * Test SIOCGSKNS fails on non-socket file descriptors.
 */
TEST(siocgskns_non_socket)
{
	int fd;
	int pipefd[2];

	/* Test on regular file */
	fd = open("/dev/null", O_RDONLY);
	ASSERT_GE(fd, 0);

	ASSERT_LT(ioctl(fd, SIOCGSKNS), 0);
	ASSERT_TRUE(errno == ENOTTY || errno == EINVAL);
	close(fd);

	/* Test on pipe */
	ASSERT_EQ(pipe(pipefd), 0);

	ASSERT_LT(ioctl(pipefd[0], SIOCGSKNS), 0);
	ASSERT_TRUE(errno == ENOTTY || errno == EINVAL);

	close(pipefd[0]);
	close(pipefd[1]);
}

TEST_HARNESS_MAIN
