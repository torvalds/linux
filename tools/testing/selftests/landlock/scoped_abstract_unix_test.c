// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Abstract UNIX socket
 *
 * Copyright © 2024 Tahera Fahimi <fahimitahera@gmail.com>
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "scoped_common.h"

/* Number of pending connections queue to be hold. */
const short backlog = 10;

static void create_fs_domain(struct __test_metadata *const _metadata)
{
	int ruleset_fd;
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
	};

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	EXPECT_LE(0, ruleset_fd)
	{
		TH_LOG("Failed to create a ruleset: %s", strerror(errno));
	}
	EXPECT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	EXPECT_EQ(0, landlock_restrict_self(ruleset_fd, 0));
	EXPECT_EQ(0, close(ruleset_fd));
}

FIXTURE(scoped_domains)
{
	struct service_fixture stream_address, dgram_address;
};

#include "scoped_base_variants.h"

FIXTURE_SETUP(scoped_domains)
{
	drop_caps(_metadata);

	memset(&self->stream_address, 0, sizeof(self->stream_address));
	memset(&self->dgram_address, 0, sizeof(self->dgram_address));
	set_unix_address(&self->stream_address, 0);
	set_unix_address(&self->dgram_address, 1);
}

FIXTURE_TEARDOWN(scoped_domains)
{
}

/*
 * Test unix_stream_connect() and unix_may_send() for a child connecting to its
 * parent, when they have scoped domain or no domain.
 */
TEST_F(scoped_domains, connect_to_parent)
{
	pid_t child;
	bool can_connect_to_parent;
	int status;
	int pipe_parent[2];
	int stream_server, dgram_server;

	/*
	 * can_connect_to_parent is true if a child process can connect to its
	 * parent process. This depends on the child process not being isolated
	 * from the parent with a dedicated Landlock domain.
	 */
	can_connect_to_parent = !variant->domain_child;

	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	if (variant->domain_both) {
		create_scoped_domain(_metadata,
				     LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);
		if (!__test_passed(_metadata))
			return;
	}

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int err;
		int stream_client, dgram_client;
		char buf_child;

		EXPECT_EQ(0, close(pipe_parent[1]));
		if (variant->domain_child)
			create_scoped_domain(
				_metadata, LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

		stream_client = socket(AF_UNIX, SOCK_STREAM, 0);
		ASSERT_LE(0, stream_client);
		dgram_client = socket(AF_UNIX, SOCK_DGRAM, 0);
		ASSERT_LE(0, dgram_client);

		/* Waits for the server. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));

		err = connect(stream_client, &self->stream_address.unix_addr,
			      self->stream_address.unix_addr_len);
		if (can_connect_to_parent) {
			EXPECT_EQ(0, err);
		} else {
			EXPECT_EQ(-1, err);
			EXPECT_EQ(EPERM, errno);
		}
		EXPECT_EQ(0, close(stream_client));

		err = connect(dgram_client, &self->dgram_address.unix_addr,
			      self->dgram_address.unix_addr_len);
		if (can_connect_to_parent) {
			EXPECT_EQ(0, err);
		} else {
			EXPECT_EQ(-1, err);
			EXPECT_EQ(EPERM, errno);
		}
		EXPECT_EQ(0, close(dgram_client));
		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(0, close(pipe_parent[0]));
	if (variant->domain_parent)
		create_scoped_domain(_metadata,
				     LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

	stream_server = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_LE(0, stream_server);
	dgram_server = socket(AF_UNIX, SOCK_DGRAM, 0);
	ASSERT_LE(0, dgram_server);
	ASSERT_EQ(0, bind(stream_server, &self->stream_address.unix_addr,
			  self->stream_address.unix_addr_len));
	ASSERT_EQ(0, bind(dgram_server, &self->dgram_address.unix_addr,
			  self->dgram_address.unix_addr_len));
	ASSERT_EQ(0, listen(stream_server, backlog));

	/* Signals to child that the parent is listening. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));

	ASSERT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(0, close(stream_server));
	EXPECT_EQ(0, close(dgram_server));

	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;
}

/*
 * Test unix_stream_connect() and unix_may_send() for a parent connecting to
 * its child, when they have scoped domain or no domain.
 */
TEST_F(scoped_domains, connect_to_child)
{
	pid_t child;
	bool can_connect_to_child;
	int err_stream, err_dgram, errno_stream, errno_dgram, status;
	int pipe_child[2], pipe_parent[2];
	char buf;
	int stream_client, dgram_client;

	/*
	 * can_connect_to_child is true if a parent process can connect to its
	 * child process. The parent process is not isolated from the child
	 * with a dedicated Landlock domain.
	 */
	can_connect_to_child = !variant->domain_parent;

	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	if (variant->domain_both) {
		create_scoped_domain(_metadata,
				     LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);
		if (!__test_passed(_metadata))
			return;
	}

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int stream_server, dgram_server;

		EXPECT_EQ(0, close(pipe_parent[1]));
		EXPECT_EQ(0, close(pipe_child[0]));
		if (variant->domain_child)
			create_scoped_domain(
				_metadata, LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

		/* Waits for the parent to be in a domain, if any. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf, 1));

		stream_server = socket(AF_UNIX, SOCK_STREAM, 0);
		ASSERT_LE(0, stream_server);
		dgram_server = socket(AF_UNIX, SOCK_DGRAM, 0);
		ASSERT_LE(0, dgram_server);
		ASSERT_EQ(0,
			  bind(stream_server, &self->stream_address.unix_addr,
			       self->stream_address.unix_addr_len));
		ASSERT_EQ(0, bind(dgram_server, &self->dgram_address.unix_addr,
				  self->dgram_address.unix_addr_len));
		ASSERT_EQ(0, listen(stream_server, backlog));

		/* Signals to the parent that child is listening. */
		ASSERT_EQ(1, write(pipe_child[1], ".", 1));

		/* Waits to connect. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf, 1));
		EXPECT_EQ(0, close(stream_server));
		EXPECT_EQ(0, close(dgram_server));
		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(0, close(pipe_child[1]));
	EXPECT_EQ(0, close(pipe_parent[0]));

	if (variant->domain_parent)
		create_scoped_domain(_metadata,
				     LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

	/* Signals that the parent is in a domain, if any. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));

	stream_client = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_LE(0, stream_client);
	dgram_client = socket(AF_UNIX, SOCK_DGRAM, 0);
	ASSERT_LE(0, dgram_client);

	/* Waits for the child to listen */
	ASSERT_EQ(1, read(pipe_child[0], &buf, 1));
	err_stream = connect(stream_client, &self->stream_address.unix_addr,
			     self->stream_address.unix_addr_len);
	errno_stream = errno;
	err_dgram = connect(dgram_client, &self->dgram_address.unix_addr,
			    self->dgram_address.unix_addr_len);
	errno_dgram = errno;
	if (can_connect_to_child) {
		EXPECT_EQ(0, err_stream);
		EXPECT_EQ(0, err_dgram);
	} else {
		EXPECT_EQ(-1, err_stream);
		EXPECT_EQ(-1, err_dgram);
		EXPECT_EQ(EPERM, errno_stream);
		EXPECT_EQ(EPERM, errno_dgram);
	}
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(0, close(stream_client));
	EXPECT_EQ(0, close(dgram_client));

	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;
}

FIXTURE(scoped_vs_unscoped)
{
	struct service_fixture parent_stream_address, parent_dgram_address,
		child_stream_address, child_dgram_address;
};

#include "scoped_multiple_domain_variants.h"

FIXTURE_SETUP(scoped_vs_unscoped)
{
	drop_caps(_metadata);

	memset(&self->parent_stream_address, 0,
	       sizeof(self->parent_stream_address));
	set_unix_address(&self->parent_stream_address, 0);
	memset(&self->parent_dgram_address, 0,
	       sizeof(self->parent_dgram_address));
	set_unix_address(&self->parent_dgram_address, 1);
	memset(&self->child_stream_address, 0,
	       sizeof(self->child_stream_address));
	set_unix_address(&self->child_stream_address, 2);
	memset(&self->child_dgram_address, 0,
	       sizeof(self->child_dgram_address));
	set_unix_address(&self->child_dgram_address, 3);
}

FIXTURE_TEARDOWN(scoped_vs_unscoped)
{
}

/*
 * Test unix_stream_connect and unix_may_send for parent, child and
 * grand child processes when they can have scoped or non-scoped domains.
 */
TEST_F(scoped_vs_unscoped, unix_scoping)
{
	pid_t child;
	int status;
	bool can_connect_to_parent, can_connect_to_child;
	int pipe_parent[2];
	int stream_server_parent, dgram_server_parent;

	can_connect_to_child = (variant->domain_grand_child != SCOPE_SANDBOX);
	can_connect_to_parent = (can_connect_to_child &&
				 (variant->domain_children != SCOPE_SANDBOX));

	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));

	if (variant->domain_all == OTHER_SANDBOX)
		create_fs_domain(_metadata);
	else if (variant->domain_all == SCOPE_SANDBOX)
		create_scoped_domain(_metadata,
				     LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int stream_server_child, dgram_server_child;
		int pipe_child[2];
		pid_t grand_child;

		ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));

		if (variant->domain_children == OTHER_SANDBOX)
			create_fs_domain(_metadata);
		else if (variant->domain_children == SCOPE_SANDBOX)
			create_scoped_domain(
				_metadata, LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

		grand_child = fork();
		ASSERT_LE(0, grand_child);
		if (grand_child == 0) {
			char buf;
			int stream_err, dgram_err, stream_errno, dgram_errno;
			int stream_client, dgram_client;

			EXPECT_EQ(0, close(pipe_parent[1]));
			EXPECT_EQ(0, close(pipe_child[1]));

			if (variant->domain_grand_child == OTHER_SANDBOX)
				create_fs_domain(_metadata);
			else if (variant->domain_grand_child == SCOPE_SANDBOX)
				create_scoped_domain(
					_metadata,
					LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

			stream_client = socket(AF_UNIX, SOCK_STREAM, 0);
			ASSERT_LE(0, stream_client);
			dgram_client = socket(AF_UNIX, SOCK_DGRAM, 0);
			ASSERT_LE(0, dgram_client);

			ASSERT_EQ(1, read(pipe_child[0], &buf, 1));
			stream_err = connect(
				stream_client,
				&self->child_stream_address.unix_addr,
				self->child_stream_address.unix_addr_len);
			stream_errno = errno;
			dgram_err = connect(
				dgram_client,
				&self->child_dgram_address.unix_addr,
				self->child_dgram_address.unix_addr_len);
			dgram_errno = errno;
			if (can_connect_to_child) {
				EXPECT_EQ(0, stream_err);
				EXPECT_EQ(0, dgram_err);
			} else {
				EXPECT_EQ(-1, stream_err);
				EXPECT_EQ(-1, dgram_err);
				EXPECT_EQ(EPERM, stream_errno);
				EXPECT_EQ(EPERM, dgram_errno);
			}

			EXPECT_EQ(0, close(stream_client));
			stream_client = socket(AF_UNIX, SOCK_STREAM, 0);
			ASSERT_LE(0, stream_client);
			/* Datagram sockets can "reconnect". */

			ASSERT_EQ(1, read(pipe_parent[0], &buf, 1));
			stream_err = connect(
				stream_client,
				&self->parent_stream_address.unix_addr,
				self->parent_stream_address.unix_addr_len);
			stream_errno = errno;
			dgram_err = connect(
				dgram_client,
				&self->parent_dgram_address.unix_addr,
				self->parent_dgram_address.unix_addr_len);
			dgram_errno = errno;
			if (can_connect_to_parent) {
				EXPECT_EQ(0, stream_err);
				EXPECT_EQ(0, dgram_err);
			} else {
				EXPECT_EQ(-1, stream_err);
				EXPECT_EQ(-1, dgram_err);
				EXPECT_EQ(EPERM, stream_errno);
				EXPECT_EQ(EPERM, dgram_errno);
			}
			EXPECT_EQ(0, close(stream_client));
			EXPECT_EQ(0, close(dgram_client));

			_exit(_metadata->exit_code);
			return;
		}
		EXPECT_EQ(0, close(pipe_child[0]));
		if (variant->domain_child == OTHER_SANDBOX)
			create_fs_domain(_metadata);
		else if (variant->domain_child == SCOPE_SANDBOX)
			create_scoped_domain(
				_metadata, LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

		stream_server_child = socket(AF_UNIX, SOCK_STREAM, 0);
		ASSERT_LE(0, stream_server_child);
		dgram_server_child = socket(AF_UNIX, SOCK_DGRAM, 0);
		ASSERT_LE(0, dgram_server_child);

		ASSERT_EQ(0, bind(stream_server_child,
				  &self->child_stream_address.unix_addr,
				  self->child_stream_address.unix_addr_len));
		ASSERT_EQ(0, bind(dgram_server_child,
				  &self->child_dgram_address.unix_addr,
				  self->child_dgram_address.unix_addr_len));
		ASSERT_EQ(0, listen(stream_server_child, backlog));

		ASSERT_EQ(1, write(pipe_child[1], ".", 1));
		ASSERT_EQ(grand_child, waitpid(grand_child, &status, 0));
		EXPECT_EQ(0, close(stream_server_child))
		EXPECT_EQ(0, close(dgram_server_child));
		return;
	}
	EXPECT_EQ(0, close(pipe_parent[0]));

	if (variant->domain_parent == OTHER_SANDBOX)
		create_fs_domain(_metadata);
	else if (variant->domain_parent == SCOPE_SANDBOX)
		create_scoped_domain(_metadata,
				     LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

	stream_server_parent = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_LE(0, stream_server_parent);
	dgram_server_parent = socket(AF_UNIX, SOCK_DGRAM, 0);
	ASSERT_LE(0, dgram_server_parent);
	ASSERT_EQ(0, bind(stream_server_parent,
			  &self->parent_stream_address.unix_addr,
			  self->parent_stream_address.unix_addr_len));
	ASSERT_EQ(0, bind(dgram_server_parent,
			  &self->parent_dgram_address.unix_addr,
			  self->parent_dgram_address.unix_addr_len));

	ASSERT_EQ(0, listen(stream_server_parent, backlog));

	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(0, close(stream_server_parent));
	EXPECT_EQ(0, close(dgram_server_parent));

	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;
}

FIXTURE(outside_socket)
{
	struct service_fixture address, transit_address;
};

FIXTURE_VARIANT(outside_socket)
{
	const bool child_socket;
	const int type;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(outside_socket, allow_dgram_child) {
	/* clang-format on */
	.child_socket = true,
	.type = SOCK_DGRAM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(outside_socket, deny_dgram_server) {
	/* clang-format on */
	.child_socket = false,
	.type = SOCK_DGRAM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(outside_socket, allow_stream_child) {
	/* clang-format on */
	.child_socket = true,
	.type = SOCK_STREAM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(outside_socket, deny_stream_server) {
	/* clang-format on */
	.child_socket = false,
	.type = SOCK_STREAM,
};

FIXTURE_SETUP(outside_socket)
{
	drop_caps(_metadata);

	memset(&self->transit_address, 0, sizeof(self->transit_address));
	set_unix_address(&self->transit_address, 0);
	memset(&self->address, 0, sizeof(self->address));
	set_unix_address(&self->address, 1);
}

FIXTURE_TEARDOWN(outside_socket)
{
}

/*
 * Test unix_stream_connect and unix_may_send for parent and child processes
 * when connecting socket has different domain than the process using it.
 */
TEST_F(outside_socket, socket_with_different_domain)
{
	pid_t child;
	int err, status;
	int pipe_child[2], pipe_parent[2];
	char buf_parent;
	int server_socket;

	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int client_socket;
		char buf_child;

		EXPECT_EQ(0, close(pipe_parent[1]));
		EXPECT_EQ(0, close(pipe_child[0]));

		/* Client always has a domain. */
		create_scoped_domain(_metadata,
				     LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

		if (variant->child_socket) {
			int data_socket, passed_socket, stream_server;

			passed_socket = socket(AF_UNIX, variant->type, 0);
			ASSERT_LE(0, passed_socket);
			stream_server = socket(AF_UNIX, SOCK_STREAM, 0);
			ASSERT_LE(0, stream_server);
			ASSERT_EQ(0, bind(stream_server,
					  &self->transit_address.unix_addr,
					  self->transit_address.unix_addr_len));
			ASSERT_EQ(0, listen(stream_server, backlog));
			ASSERT_EQ(1, write(pipe_child[1], ".", 1));
			data_socket = accept(stream_server, NULL, NULL);
			ASSERT_LE(0, data_socket);
			ASSERT_EQ(0, send_fd(data_socket, passed_socket));
			EXPECT_EQ(0, close(passed_socket));
			EXPECT_EQ(0, close(stream_server));
		}

		client_socket = socket(AF_UNIX, variant->type, 0);
		ASSERT_LE(0, client_socket);

		/* Waits for parent signal for connection. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));
		err = connect(client_socket, &self->address.unix_addr,
			      self->address.unix_addr_len);
		if (variant->child_socket) {
			EXPECT_EQ(0, err);
		} else {
			EXPECT_EQ(-1, err);
			EXPECT_EQ(EPERM, errno);
		}
		EXPECT_EQ(0, close(client_socket));
		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(0, close(pipe_child[1]));
	EXPECT_EQ(0, close(pipe_parent[0]));

	if (variant->child_socket) {
		int client_child = socket(AF_UNIX, SOCK_STREAM, 0);

		ASSERT_LE(0, client_child);
		ASSERT_EQ(1, read(pipe_child[0], &buf_parent, 1));
		ASSERT_EQ(0, connect(client_child,
				     &self->transit_address.unix_addr,
				     self->transit_address.unix_addr_len));
		server_socket = recv_fd(client_child);
		EXPECT_EQ(0, close(client_child));
	} else {
		server_socket = socket(AF_UNIX, variant->type, 0);
	}
	ASSERT_LE(0, server_socket);

	/* Server always has a domain. */
	create_scoped_domain(_metadata, LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET);

	ASSERT_EQ(0, bind(server_socket, &self->address.unix_addr,
			  self->address.unix_addr_len));
	if (variant->type == SOCK_STREAM)
		ASSERT_EQ(0, listen(server_socket, backlog));

	/* Signals to child that the parent is listening. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));

	ASSERT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(0, close(server_socket));

	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;
}

TEST_HARNESS_MAIN
