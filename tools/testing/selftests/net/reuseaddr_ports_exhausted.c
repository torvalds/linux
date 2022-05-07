// SPDX-License-Identifier: GPL-2.0-only
/*
 * Check if we can fully utilize 4-tuples for connect().
 *
 * Rules to bind sockets to the same port when all ephemeral ports are
 * exhausted.
 *
 *   1. if there are TCP_LISTEN sockets on the port, fail to bind.
 *   2. if there are sockets without SO_REUSEADDR, fail to bind.
 *   3. if SO_REUSEADDR is disabled, fail to bind.
 *   4. if SO_REUSEADDR is enabled and SO_REUSEPORT is disabled,
 *        succeed to bind.
 *   5. if SO_REUSEADDR and SO_REUSEPORT are enabled and
 *        there is no socket having the both options and the same EUID,
 *        succeed to bind.
 *   6. fail to bind.
 *
 * Author: Kuniyuki Iwashima <kuniyu@amazon.co.jp>
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "../kselftest_harness.h"

struct reuse_opts {
	int reuseaddr[2];
	int reuseport[2];
};

struct reuse_opts unreusable_opts[12] = {
	{0, 0, 0, 0},
	{0, 0, 0, 1},
	{0, 0, 1, 0},
	{0, 0, 1, 1},
	{0, 1, 0, 0},
	{0, 1, 0, 1},
	{0, 1, 1, 0},
	{0, 1, 1, 1},
	{1, 0, 0, 0},
	{1, 0, 0, 1},
	{1, 0, 1, 0},
	{1, 0, 1, 1},
};

struct reuse_opts reusable_opts[4] = {
	{1, 1, 0, 0},
	{1, 1, 0, 1},
	{1, 1, 1, 0},
	{1, 1, 1, 1},
};

int bind_port(struct __test_metadata *_metadata, int reuseaddr, int reuseport)
{
	struct sockaddr_in local_addr;
	int len = sizeof(local_addr);
	int fd, ret;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_NE(-1, fd) TH_LOG("failed to open socket.");

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int));
	ASSERT_EQ(0, ret) TH_LOG("failed to setsockopt: SO_REUSEADDR.");

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuseport, sizeof(int));
	ASSERT_EQ(0, ret) TH_LOG("failed to setsockopt: SO_REUSEPORT.");

	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	local_addr.sin_port = 0;

	if (bind(fd, (struct sockaddr *)&local_addr, len) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}

TEST(reuseaddr_ports_exhausted_unreusable)
{
	struct reuse_opts *opts;
	int i, j, fd[2];

	for (i = 0; i < 12; i++) {
		opts = &unreusable_opts[i];

		for (j = 0; j < 2; j++)
			fd[j] = bind_port(_metadata, opts->reuseaddr[j], opts->reuseport[j]);

		ASSERT_NE(-1, fd[0]) TH_LOG("failed to bind.");
		EXPECT_EQ(-1, fd[1]) TH_LOG("should fail to bind.");

		for (j = 0; j < 2; j++)
			if (fd[j] != -1)
				close(fd[j]);
	}
}

TEST(reuseaddr_ports_exhausted_reusable_same_euid)
{
	struct reuse_opts *opts;
	int i, j, fd[2];

	for (i = 0; i < 4; i++) {
		opts = &reusable_opts[i];

		for (j = 0; j < 2; j++)
			fd[j] = bind_port(_metadata, opts->reuseaddr[j], opts->reuseport[j]);

		ASSERT_NE(-1, fd[0]) TH_LOG("failed to bind.");

		if (opts->reuseport[0] && opts->reuseport[1]) {
			EXPECT_EQ(-1, fd[1]) TH_LOG("should fail to bind because both sockets succeed to be listened.");
		} else {
			EXPECT_NE(-1, fd[1]) TH_LOG("should succeed to bind to connect to different destinations.");
		}

		for (j = 0; j < 2; j++)
			if (fd[j] != -1)
				close(fd[j]);
	}
}

TEST(reuseaddr_ports_exhausted_reusable_different_euid)
{
	struct reuse_opts *opts;
	int i, j, ret, fd[2];
	uid_t euid[2] = {10, 20};

	for (i = 0; i < 4; i++) {
		opts = &reusable_opts[i];

		for (j = 0; j < 2; j++) {
			ret = seteuid(euid[j]);
			ASSERT_EQ(0, ret) TH_LOG("failed to seteuid: %d.", euid[j]);

			fd[j] = bind_port(_metadata, opts->reuseaddr[j], opts->reuseport[j]);

			ret = seteuid(0);
			ASSERT_EQ(0, ret) TH_LOG("failed to seteuid: 0.");
		}

		ASSERT_NE(-1, fd[0]) TH_LOG("failed to bind.");
		EXPECT_NE(-1, fd[1]) TH_LOG("should succeed to bind because one socket can be bound in each euid.");

		if (fd[1] != -1) {
			ret = listen(fd[0], 5);
			ASSERT_EQ(0, ret) TH_LOG("failed to listen.");

			ret = listen(fd[1], 5);
			EXPECT_EQ(-1, ret) TH_LOG("should fail to listen because only one uid reserves the port in TCP_LISTEN.");
		}

		for (j = 0; j < 2; j++)
			if (fd[j] != -1)
				close(fd[j]);
	}
}

TEST_HARNESS_MAIN
