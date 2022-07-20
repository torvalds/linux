// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <sched.h>

#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "../../kselftest_harness.h"

FIXTURE(unix_connect)
{
	int server, client;
	int family;
};

FIXTURE_VARIANT(unix_connect)
{
	int type;
	char sun_path[8];
	int len;
	int flags;
	int err;
};

FIXTURE_VARIANT_ADD(unix_connect, stream_pathname)
{
	.type = SOCK_STREAM,
	.sun_path = "test",
	.len = 4 + 1,
	.flags = 0,
	.err = 0,
};

FIXTURE_VARIANT_ADD(unix_connect, stream_abstract)
{
	.type = SOCK_STREAM,
	.sun_path = "\0test",
	.len = 5,
	.flags = 0,
	.err = 0,
};

FIXTURE_VARIANT_ADD(unix_connect, stream_pathname_netns)
{
	.type = SOCK_STREAM,
	.sun_path = "test",
	.len = 4 + 1,
	.flags = CLONE_NEWNET,
	.err = 0,
};

FIXTURE_VARIANT_ADD(unix_connect, stream_abstract_netns)
{
	.type = SOCK_STREAM,
	.sun_path = "\0test",
	.len = 5,
	.flags = CLONE_NEWNET,
	.err = ECONNREFUSED,
};

FIXTURE_VARIANT_ADD(unix_connect, dgram_pathname)
{
	.type = SOCK_DGRAM,
	.sun_path = "test",
	.len = 4 + 1,
	.flags = 0,
	.err = 0,
};

FIXTURE_VARIANT_ADD(unix_connect, dgram_abstract)
{
	.type = SOCK_DGRAM,
	.sun_path = "\0test",
	.len = 5,
	.flags = 0,
	.err = 0,
};

FIXTURE_VARIANT_ADD(unix_connect, dgram_pathname_netns)
{
	.type = SOCK_DGRAM,
	.sun_path = "test",
	.len = 4 + 1,
	.flags = CLONE_NEWNET,
	.err = 0,
};

FIXTURE_VARIANT_ADD(unix_connect, dgram_abstract_netns)
{
	.type = SOCK_DGRAM,
	.sun_path = "\0test",
	.len = 5,
	.flags = CLONE_NEWNET,
	.err = ECONNREFUSED,
};

FIXTURE_SETUP(unix_connect)
{
	self->family = AF_UNIX;
}

FIXTURE_TEARDOWN(unix_connect)
{
	close(self->server);
	close(self->client);

	if (variant->sun_path[0])
		remove("test");
}

TEST_F(unix_connect, test)
{
	socklen_t addrlen;
	struct sockaddr_un addr = {
		.sun_family = self->family,
	};
	int err;

	self->server = socket(self->family, variant->type, 0);
	ASSERT_NE(-1, self->server);

	addrlen = offsetof(struct sockaddr_un, sun_path) + variant->len;
	memcpy(&addr.sun_path, variant->sun_path, variant->len);

	err = bind(self->server, (struct sockaddr *)&addr, addrlen);
	ASSERT_EQ(0, err);

	if (variant->type == SOCK_STREAM) {
		err = listen(self->server, 32);
		ASSERT_EQ(0, err);
	}

	err = unshare(variant->flags);
	ASSERT_EQ(0, err);

	self->client = socket(self->family, variant->type, 0);
	ASSERT_LT(0, self->client);

	err = connect(self->client, (struct sockaddr *)&addr, addrlen);
	ASSERT_EQ(variant->err, err == -1 ? errno : 0);
}

TEST_HARNESS_MAIN
