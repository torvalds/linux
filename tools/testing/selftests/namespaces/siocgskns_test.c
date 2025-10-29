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

/*
 * Test multiple sockets keep the same network namespace active.
 * Create multiple sockets, verify closing some doesn't affect others.
 */
TEST(siocgskns_multiple_sockets)
{
	int socks[5];
	int netns_fds[5];
	int i;
	struct stat st;
	ino_t netns_ino;

	/* Create new network namespace */
	ASSERT_EQ(unshare(CLONE_NEWNET), 0);

	/* Create multiple sockets */
	for (i = 0; i < 5; i++) {
		socks[i] = socket(AF_INET, SOCK_STREAM, 0);
		ASSERT_GE(socks[i], 0);
	}

	/* Get netns from all sockets */
	for (i = 0; i < 5; i++) {
		netns_fds[i] = ioctl(socks[i], SIOCGSKNS);
		if (netns_fds[i] < 0) {
			int j;
			for (j = 0; j <= i; j++) {
				close(socks[j]);
				if (j < i && netns_fds[j] >= 0)
					close(netns_fds[j]);
			}
			if (errno == ENOTTY || errno == EINVAL)
				SKIP(return, "SIOCGSKNS not supported");
			ASSERT_GE(netns_fds[i], 0);
		}
	}

	/* Verify all point to same netns */
	ASSERT_EQ(fstat(netns_fds[0], &st), 0);
	netns_ino = st.st_ino;

	for (i = 1; i < 5; i++) {
		ASSERT_EQ(fstat(netns_fds[i], &st), 0);
		ASSERT_EQ(st.st_ino, netns_ino);
	}

	/* Close some sockets */
	for (i = 0; i < 3; i++) {
		close(socks[i]);
	}

	/* Remaining netns FDs should still be valid */
	for (i = 3; i < 5; i++) {
		char path[64];
		snprintf(path, sizeof(path), "/proc/self/fd/%d", netns_fds[i]);
		int test_fd = open(path, O_RDONLY);
		ASSERT_GE(test_fd, 0);
		close(test_fd);
	}

	/* Cleanup */
	for (i = 0; i < 5; i++) {
		if (i >= 3)
			close(socks[i]);
		close(netns_fds[i]);
	}
}

/*
 * Test socket keeps netns active after creating process exits.
 * Verify that as long as the socket FD exists, the namespace remains active.
 */
TEST(siocgskns_netns_lifecycle)
{
	int sock_fd, netns_fd;
	int ipc_sockets[2];
	int syncpipe[2];
	pid_t pid;
	int status;
	char sync_byte;
	struct stat st;
	ino_t netns_ino;

	EXPECT_EQ(socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);

	ASSERT_EQ(pipe(syncpipe), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child */
		close(ipc_sockets[0]);
		close(syncpipe[1]);

		if (unshare(CLONE_NEWNET) < 0) {
			close(ipc_sockets[1]);
			close(syncpipe[0]);
			exit(1);
		}

		sock_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (sock_fd < 0) {
			close(ipc_sockets[1]);
			close(syncpipe[0]);
			exit(1);
		}

		/* Send socket to parent */
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
			close(syncpipe[0]);
			exit(1);
		}

		close(sock_fd);
		close(ipc_sockets[1]);

		/* Wait for parent signal */
		read(syncpipe[0], &sync_byte, 1);
		close(syncpipe[0]);
		exit(0);
	}

	/* Parent */
	close(ipc_sockets[1]);
	close(syncpipe[0]);

	/* Receive socket FD */
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
	memcpy(&sock_fd, CMSG_DATA(cmsg), sizeof(int));

	/* Get netns from socket while child is alive */
	netns_fd = ioctl(sock_fd, SIOCGSKNS);
	if (netns_fd < 0) {
		sync_byte = 'G';
		write(syncpipe[1], &sync_byte, 1);
		close(syncpipe[1]);
		close(sock_fd);
		waitpid(pid, NULL, 0);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_fd, 0);
	}
	ASSERT_EQ(fstat(netns_fd, &st), 0);
	netns_ino = st.st_ino;

	/* Signal child to exit */
	sync_byte = 'G';
	write(syncpipe[1], &sync_byte, 1);
	close(syncpipe[1]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));

	/*
	 * Socket FD should still keep namespace active even after
	 * the creating process exited.
	 */
	int test_fd = ioctl(sock_fd, SIOCGSKNS);
	ASSERT_GE(test_fd, 0);

	struct stat st_test;
	ASSERT_EQ(fstat(test_fd, &st_test), 0);
	ASSERT_EQ(st_test.st_ino, netns_ino);

	close(test_fd);
	close(netns_fd);

	/* Close socket - namespace should become inactive */
	close(sock_fd);
}

/*
 * Test IPv6 sockets also work with SIOCGSKNS.
 */
TEST(siocgskns_ipv6)
{
	int sock_fd, netns_fd, current_netns_fd;
	struct stat st1, st2;

	/* Create an IPv6 TCP socket */
	sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
	ASSERT_GE(sock_fd, 0);

	/* Use SIOCGSKNS */
	netns_fd = ioctl(sock_fd, SIOCGSKNS);
	if (netns_fd < 0) {
		close(sock_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_fd, 0);
	}

	/* Verify it matches current namespace */
	current_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	ASSERT_GE(current_netns_fd, 0);

	ASSERT_EQ(fstat(netns_fd, &st1), 0);
	ASSERT_EQ(fstat(current_netns_fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);

	close(sock_fd);
	close(netns_fd);
	close(current_netns_fd);
}

/*
 * Test that socket-kept netns appears in listns() output.
 * Verify that a network namespace kept alive by a socket FD appears in
 * listns() output even after the creating process exits, and that it
 * disappears when the socket is closed.
 */
TEST(siocgskns_listns_visibility)
{
	int sock_fd, netns_fd, owner_fd;
	int ipc_sockets[2];
	pid_t pid;
	int status;
	__u64 netns_id, owner_id;
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = CLONE_NEWNET,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 ns_ids[256];
	int ret, i;
	bool found_netns = false;

	EXPECT_EQ(socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child: create new netns and socket */
		close(ipc_sockets[0]);

		if (unshare(CLONE_NEWNET) < 0) {
			close(ipc_sockets[1]);
			exit(1);
		}

		sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock_fd < 0) {
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

	/* Get namespace ID */
	ret = ioctl(netns_fd, NS_GET_ID, &netns_id);
	if (ret < 0) {
		close(sock_fd);
		close(netns_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "NS_GET_ID not supported");
		ASSERT_EQ(ret, 0);
	}

	/* Get owner user namespace */
	owner_fd = ioctl(netns_fd, NS_GET_USERNS);
	if (owner_fd < 0) {
		close(sock_fd);
		close(netns_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "NS_GET_USERNS not supported");
		ASSERT_GE(owner_fd, 0);
	}

	/* Get owner namespace ID */
	ret = ioctl(owner_fd, NS_GET_ID, &owner_id);
	if (ret < 0) {
		close(owner_fd);
		close(sock_fd);
		close(netns_fd);
		ASSERT_EQ(ret, 0);
	}
	close(owner_fd);

	/* Namespace should appear in listns() output */
	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	if (ret < 0) {
		close(sock_fd);
		close(netns_fd);
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s", strerror(errno));
		ASSERT_GE(ret, 0);
	}

	/* Search for our network namespace in the list */
	for (i = 0; i < ret; i++) {
		if (ns_ids[i] == netns_id) {
			found_netns = true;
			break;
		}
	}

	ASSERT_TRUE(found_netns);
	TH_LOG("Found netns %llu in listns() output (kept alive by socket)", netns_id);

	/* Now verify with owner filtering */
	req.user_ns_id = owner_id;
	found_netns = false;

	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	ASSERT_GE(ret, 0);

	for (i = 0; i < ret; i++) {
		if (ns_ids[i] == netns_id) {
			found_netns = true;
			break;
		}
	}

	ASSERT_TRUE(found_netns);
	TH_LOG("Found netns %llu owned by userns %llu", netns_id, owner_id);

	/* Close socket - namespace should become inactive and disappear from listns() */
	close(sock_fd);
	close(netns_fd);

	/* Verify it's no longer in listns() output */
	req.user_ns_id = 0;
	found_netns = false;

	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	ASSERT_GE(ret, 0);

	for (i = 0; i < ret; i++) {
		if (ns_ids[i] == netns_id) {
			found_netns = true;
			break;
		}
	}

	ASSERT_FALSE(found_netns);
	TH_LOG("Netns %llu correctly disappeared from listns() after socket closed", netns_id);
}

/*
 * Test that socket-kept netns can be reopened via file handle.
 * Verify that a network namespace kept alive by a socket FD can be
 * reopened using file handles even after the creating process exits.
 */
TEST(siocgskns_file_handle)
{
	int sock_fd, netns_fd, reopened_fd;
	int ipc_sockets[2];
	pid_t pid;
	int status;
	struct stat st1, st2;
	ino_t netns_ino;
	__u64 netns_id;
	struct file_handle *handle;
	struct nsfs_file_handle *nsfs_fh;
	int ret;

	/* Allocate file_handle structure for nsfs */
	handle = malloc(sizeof(struct file_handle) + sizeof(struct nsfs_file_handle));
	ASSERT_NE(handle, NULL);
	handle->handle_bytes = sizeof(struct nsfs_file_handle);
	handle->handle_type = FILEID_NSFS;

	EXPECT_EQ(socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc_sockets), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child: create new netns and socket */
		close(ipc_sockets[0]);

		if (unshare(CLONE_NEWNET) < 0) {
			close(ipc_sockets[1]);
			exit(1);
		}

		sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock_fd < 0) {
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
	memcpy(&sock_fd, CMSG_DATA(cmsg), sizeof(int));

	/* Wait for child to exit */
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	/* Get network namespace from socket */
	netns_fd = ioctl(sock_fd, SIOCGSKNS);
	if (netns_fd < 0) {
		free(handle);
		close(sock_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_fd, 0);
	}

	ASSERT_EQ(fstat(netns_fd, &st1), 0);
	netns_ino = st1.st_ino;

	/* Get namespace ID */
	ret = ioctl(netns_fd, NS_GET_ID, &netns_id);
	if (ret < 0) {
		free(handle);
		close(sock_fd);
		close(netns_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "NS_GET_ID not supported");
		ASSERT_EQ(ret, 0);
	}

	/* Construct file handle from namespace ID */
	nsfs_fh = (struct nsfs_file_handle *)handle->f_handle;
	nsfs_fh->ns_id = netns_id;
	nsfs_fh->ns_type = 0;  /* Type field not needed for reopening */
	nsfs_fh->ns_inum = 0;  /* Inum field not needed for reopening */

	TH_LOG("Constructed file handle for netns %lu (id=%llu)", netns_ino, netns_id);

	/* Reopen namespace using file handle (while socket still keeps it alive) */
	reopened_fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (reopened_fd < 0) {
		free(handle);
		close(sock_fd);
		if (errno == EOPNOTSUPP || errno == ENOSYS || errno == EBADF)
			SKIP(return, "open_by_handle_at with FD_NSFS_ROOT not supported");
		TH_LOG("open_by_handle_at failed: %s", strerror(errno));
		ASSERT_GE(reopened_fd, 0);
	}

	/* Verify it's the same namespace */
	ASSERT_EQ(fstat(reopened_fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	TH_LOG("Successfully reopened netns %lu via file handle", netns_ino);

	close(reopened_fd);

	/* Close the netns FD */
	close(netns_fd);

	/* Try to reopen via file handle - should fail since namespace is now inactive */
	reopened_fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	ASSERT_LT(reopened_fd, 0);
	TH_LOG("Correctly failed to reopen inactive netns: %s", strerror(errno));

	/* Get network namespace from socket */
	netns_fd = ioctl(sock_fd, SIOCGSKNS);
	if (netns_fd < 0) {
		free(handle);
		close(sock_fd);
		if (errno == ENOTTY || errno == EINVAL)
			SKIP(return, "SIOCGSKNS not supported");
		ASSERT_GE(netns_fd, 0);
	}

	/* Reopen namespace using file handle (while socket still keeps it alive) */
	reopened_fd = open_by_handle_at(FD_NSFS_ROOT, handle, O_RDONLY);
	if (reopened_fd < 0) {
		free(handle);
		close(sock_fd);
		if (errno == EOPNOTSUPP || errno == ENOSYS || errno == EBADF)
			SKIP(return, "open_by_handle_at with FD_NSFS_ROOT not supported");
		TH_LOG("open_by_handle_at failed: %s", strerror(errno));
		ASSERT_GE(reopened_fd, 0);
	}

	/* Verify it's the same namespace */
	ASSERT_EQ(fstat(reopened_fd, &st2), 0);
	ASSERT_EQ(st1.st_ino, st2.st_ino);
	ASSERT_EQ(st1.st_dev, st2.st_dev);

	TH_LOG("Successfully reopened netns %lu via file handle", netns_ino);

	/* Close socket - namespace should become inactive */
	close(sock_fd);
	free(handle);
}

TEST_HARNESS_MAIN
