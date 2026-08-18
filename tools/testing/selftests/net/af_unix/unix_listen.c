// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for the state checks in AF_UNIX listen().
 *
 * The central case is a regression test: listen() on a bound socket that
 * is already connected (i.e. not in TCP_CLOSE or TCP_LISTEN state) must
 * fail with EINVAL.  A prior change accidentally let it return success
 * without doing anything, because a helper called in between reset the
 * error code to 0.  The neighbouring checks (unbound, already listening)
 * are tested too so they cannot silently regress the same way.
 *
 * Every case runs for both listenable socket types (SOCK_STREAM and
 * SOCK_SEQPACKET) and both pathname and abstract addresses.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "kselftest_harness.h"

#define SK_NAME		"unix_listen_sk"
#define SRV_NAME	"unix_listen_srv"

FIXTURE(unix_listen)
{
	int sk;			/* socket under test */
	int server;		/* a listening peer, when a test needs one */
	struct sockaddr_un addr, srv_addr;
	socklen_t addrlen, srv_addrlen;
};

FIXTURE_VARIANT(unix_listen)
{
	int type;
	int abstract;
};

FIXTURE_VARIANT_ADD(unix_listen, stream_pathname)
{
	.type = SOCK_STREAM,
	.abstract = 0,
};

FIXTURE_VARIANT_ADD(unix_listen, stream_abstract)
{
	.type = SOCK_STREAM,
	.abstract = 1,
};

FIXTURE_VARIANT_ADD(unix_listen, seqpacket_pathname)
{
	.type = SOCK_SEQPACKET,
	.abstract = 0,
};

FIXTURE_VARIANT_ADD(unix_listen, seqpacket_abstract)
{
	.type = SOCK_SEQPACKET,
	.abstract = 1,
};

/* Fill @addr with a pathname or abstract address named @name. */
static socklen_t unix_set_addr(struct sockaddr_un *addr, const char *name,
			       int abstract)
{
	size_t len = strlen(name);

	memset(addr, 0, sizeof(*addr));
	addr->sun_family = AF_UNIX;
	/* An abstract address leads with a NUL and has no filesystem entry. */
	memcpy(addr->sun_path + (abstract ? 1 : 0), name, len);

	return offsetof(struct sockaddr_un, sun_path) + len + 1;
}

FIXTURE_SETUP(unix_listen)
{
	self->sk = -1;
	self->server = -1;
	self->addrlen = unix_set_addr(&self->addr, SK_NAME, variant->abstract);
	self->srv_addrlen = unix_set_addr(&self->srv_addr, SRV_NAME,
					  variant->abstract);
}

FIXTURE_TEARDOWN(unix_listen)
{
	if (self->sk >= 0)
		close(self->sk);
	if (self->server >= 0)
		close(self->server);

	/* Pathname sockets leave a filesystem entry behind; abstract ones do not. */
	if (!variant->abstract) {
		remove(SK_NAME);
		remove(SRV_NAME);
	}
}

/* A bound socket in TCP_CLOSE is the normal, allowed case. */
TEST_F(unix_listen, bound_is_ok)
{
	int err;

	self->sk = socket(AF_UNIX, variant->type, 0);
	ASSERT_LE(0, self->sk);

	err = bind(self->sk, (struct sockaddr *)&self->addr, self->addrlen);
	ASSERT_EQ(0, err);

	err = listen(self->sk, 8);
	EXPECT_EQ(0, err);
}

/* Listening again on an already-listening socket (TCP_LISTEN) is allowed. */
TEST_F(unix_listen, relisten_is_ok)
{
	int err;

	self->sk = socket(AF_UNIX, variant->type, 0);
	ASSERT_LE(0, self->sk);

	err = bind(self->sk, (struct sockaddr *)&self->addr, self->addrlen);
	ASSERT_EQ(0, err);

	err = listen(self->sk, 8);
	ASSERT_EQ(0, err);

	err = listen(self->sk, 16);
	EXPECT_EQ(0, err);
}

/* listen() on an unbound socket fails: there is nothing to listen on. */
TEST_F(unix_listen, unbound_is_einval)
{
	int err;

	self->sk = socket(AF_UNIX, variant->type, 0);
	ASSERT_LE(0, self->sk);

	err = listen(self->sk, 8);
	EXPECT_EQ(-1, err);
	EXPECT_EQ(EINVAL, errno);
}

/*
 * The regression: a bound socket that has already been connected is not in
 * TCP_CLOSE or TCP_LISTEN, so listen() must reject it with EINVAL rather
 * than quietly succeeding.
 */
TEST_F(unix_listen, connected_is_einval)
{
	int err;

	self->server = socket(AF_UNIX, variant->type, 0);
	ASSERT_LE(0, self->server);

	err = bind(self->server, (struct sockaddr *)&self->srv_addr,
		   self->srv_addrlen);
	ASSERT_EQ(0, err);

	err = listen(self->server, 8);
	ASSERT_EQ(0, err);

	self->sk = socket(AF_UNIX, variant->type, 0);
	ASSERT_LE(0, self->sk);

	/* Bind first so the unbound check does not mask the state check. */
	err = bind(self->sk, (struct sockaddr *)&self->addr, self->addrlen);
	ASSERT_EQ(0, err);

	err = connect(self->sk, (struct sockaddr *)&self->srv_addr,
		      self->srv_addrlen);
	ASSERT_EQ(0, err);

	err = listen(self->sk, 8);
	EXPECT_EQ(-1, err);
	EXPECT_EQ(EINVAL, errno);
}

TEST_HARNESS_MAIN
