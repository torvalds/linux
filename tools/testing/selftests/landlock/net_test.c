// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock tests - Network
 *
 * Copyright © 2022-2023 Huawei Tech. Co., Ltd.
 * Copyright © 2023 Microsoft Corporation
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <linux/in.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>

#include "common.h"

const short sock_port_start = (1 << 10);

static const char loopback_ipv4[] = "127.0.0.1";
static const char loopback_ipv6[] = "::1";

/* Number pending connections queue to be hold. */
const short backlog = 10;

enum sandbox_type {
	NO_SANDBOX,
	/* This may be used to test rules that allow *and* deny accesses. */
	TCP_SANDBOX,
};

struct protocol_variant {
	int domain;
	int type;
};

struct service_fixture {
	struct protocol_variant protocol;
	/* port is also stored in ipv4_addr.sin_port or ipv6_addr.sin6_port */
	unsigned short port;
	union {
		struct sockaddr_in ipv4_addr;
		struct sockaddr_in6 ipv6_addr;
		struct {
			struct sockaddr_un unix_addr;
			socklen_t unix_addr_len;
		};
	};
};

static pid_t sys_gettid(void)
{
	return syscall(__NR_gettid);
}

static int set_service(struct service_fixture *const srv,
		       const struct protocol_variant prot,
		       const unsigned short index)
{
	memset(srv, 0, sizeof(*srv));

	/*
	 * Copies all protocol properties in case of the variant only contains
	 * a subset of them.
	 */
	srv->protocol = prot;

	/* Checks for port overflow. */
	if (index > 2)
		return 1;
	srv->port = sock_port_start << (2 * index);

	switch (prot.domain) {
	case AF_UNSPEC:
	case AF_INET:
		srv->ipv4_addr.sin_family = prot.domain;
		srv->ipv4_addr.sin_port = htons(srv->port);
		srv->ipv4_addr.sin_addr.s_addr = inet_addr(loopback_ipv4);
		return 0;

	case AF_INET6:
		srv->ipv6_addr.sin6_family = prot.domain;
		srv->ipv6_addr.sin6_port = htons(srv->port);
		inet_pton(AF_INET6, loopback_ipv6, &srv->ipv6_addr.sin6_addr);
		return 0;

	case AF_UNIX:
		srv->unix_addr.sun_family = prot.domain;
		sprintf(srv->unix_addr.sun_path,
			"_selftests-landlock-net-tid%d-index%d", sys_gettid(),
			index);
		srv->unix_addr_len = SUN_LEN(&srv->unix_addr);
		srv->unix_addr.sun_path[0] = '\0';
		return 0;
	}
	return 1;
}

static void setup_loopback(struct __test_metadata *const _metadata)
{
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, unshare(CLONE_NEWNET));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	set_ambient_cap(_metadata, CAP_NET_ADMIN);
	ASSERT_EQ(0, system("ip link set dev lo up"));
	clear_ambient_cap(_metadata, CAP_NET_ADMIN);
}

static bool is_restricted(const struct protocol_variant *const prot,
			  const enum sandbox_type sandbox)
{
	switch (prot->domain) {
	case AF_INET:
	case AF_INET6:
		switch (prot->type) {
		case SOCK_STREAM:
			return sandbox == TCP_SANDBOX;
		}
		break;
	}
	return false;
}

static int socket_variant(const struct service_fixture *const srv)
{
	int ret;

	ret = socket(srv->protocol.domain, srv->protocol.type | SOCK_CLOEXEC,
		     0);
	if (ret < 0)
		return -errno;
	return ret;
}

#ifndef SIN6_LEN_RFC2133
#define SIN6_LEN_RFC2133 24
#endif

static socklen_t get_addrlen(const struct service_fixture *const srv,
			     const bool minimal)
{
	switch (srv->protocol.domain) {
	case AF_UNSPEC:
	case AF_INET:
		return sizeof(srv->ipv4_addr);

	case AF_INET6:
		if (minimal)
			return SIN6_LEN_RFC2133;
		return sizeof(srv->ipv6_addr);

	case AF_UNIX:
		if (minimal)
			return sizeof(srv->unix_addr) -
			       sizeof(srv->unix_addr.sun_path);
		return srv->unix_addr_len;

	default:
		return 0;
	}
}

static void set_port(struct service_fixture *const srv, uint16_t port)
{
	switch (srv->protocol.domain) {
	case AF_UNSPEC:
	case AF_INET:
		srv->ipv4_addr.sin_port = htons(port);
		return;

	case AF_INET6:
		srv->ipv6_addr.sin6_port = htons(port);
		return;

	default:
		return;
	}
}

static uint16_t get_binded_port(int socket_fd,
				const struct protocol_variant *const prot)
{
	struct sockaddr_in ipv4_addr;
	struct sockaddr_in6 ipv6_addr;
	socklen_t ipv4_addr_len, ipv6_addr_len;

	/* Gets binded port. */
	switch (prot->domain) {
	case AF_UNSPEC:
	case AF_INET:
		ipv4_addr_len = sizeof(ipv4_addr);
		getsockname(socket_fd, &ipv4_addr, &ipv4_addr_len);
		return ntohs(ipv4_addr.sin_port);

	case AF_INET6:
		ipv6_addr_len = sizeof(ipv6_addr);
		getsockname(socket_fd, &ipv6_addr, &ipv6_addr_len);
		return ntohs(ipv6_addr.sin6_port);

	default:
		return 0;
	}
}

static int bind_variant_addrlen(const int sock_fd,
				const struct service_fixture *const srv,
				const socklen_t addrlen)
{
	int ret;

	switch (srv->protocol.domain) {
	case AF_UNSPEC:
	case AF_INET:
		ret = bind(sock_fd, &srv->ipv4_addr, addrlen);
		break;

	case AF_INET6:
		ret = bind(sock_fd, &srv->ipv6_addr, addrlen);
		break;

	case AF_UNIX:
		ret = bind(sock_fd, &srv->unix_addr, addrlen);
		break;

	default:
		errno = EAFNOSUPPORT;
		return -errno;
	}

	if (ret < 0)
		return -errno;
	return ret;
}

static int bind_variant(const int sock_fd,
			const struct service_fixture *const srv)
{
	return bind_variant_addrlen(sock_fd, srv, get_addrlen(srv, false));
}

static int connect_variant_addrlen(const int sock_fd,
				   const struct service_fixture *const srv,
				   const socklen_t addrlen)
{
	int ret;

	switch (srv->protocol.domain) {
	case AF_UNSPEC:
	case AF_INET:
		ret = connect(sock_fd, &srv->ipv4_addr, addrlen);
		break;

	case AF_INET6:
		ret = connect(sock_fd, &srv->ipv6_addr, addrlen);
		break;

	case AF_UNIX:
		ret = connect(sock_fd, &srv->unix_addr, addrlen);
		break;

	default:
		errno = -EAFNOSUPPORT;
		return -errno;
	}

	if (ret < 0)
		return -errno;
	return ret;
}

static int connect_variant(const int sock_fd,
			   const struct service_fixture *const srv)
{
	return connect_variant_addrlen(sock_fd, srv, get_addrlen(srv, false));
}

FIXTURE(protocol)
{
	struct service_fixture srv0, srv1, srv2, unspec_any0, unspec_srv0;
};

FIXTURE_VARIANT(protocol)
{
	const enum sandbox_type sandbox;
	const struct protocol_variant prot;
};

FIXTURE_SETUP(protocol)
{
	const struct protocol_variant prot_unspec = {
		.domain = AF_UNSPEC,
		.type = SOCK_STREAM,
	};

	disable_caps(_metadata);

	ASSERT_EQ(0, set_service(&self->srv0, variant->prot, 0));
	ASSERT_EQ(0, set_service(&self->srv1, variant->prot, 1));
	ASSERT_EQ(0, set_service(&self->srv2, variant->prot, 2));

	ASSERT_EQ(0, set_service(&self->unspec_srv0, prot_unspec, 0));

	ASSERT_EQ(0, set_service(&self->unspec_any0, prot_unspec, 0));
	self->unspec_any0.ipv4_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	setup_loopback(_metadata);
};

FIXTURE_TEARDOWN(protocol)
{
}

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv4_tcp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv6_tcp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv4_udp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_DGRAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv6_udp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_DGRAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_unix_stream) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_UNIX,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_unix_datagram) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_UNIX,
		.type = SOCK_DGRAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv4_tcp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv6_tcp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv4_udp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_DGRAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv6_udp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_DGRAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_unix_stream) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_UNIX,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_unix_datagram) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_UNIX,
		.type = SOCK_DGRAM,
	},
};

static void test_bind_and_connect(struct __test_metadata *const _metadata,
				  const struct service_fixture *const srv,
				  const bool deny_bind, const bool deny_connect)
{
	char buf = '\0';
	int inval_fd, bind_fd, client_fd, status, ret;
	pid_t child;

	/* Starts invalid addrlen tests with bind. */
	inval_fd = socket_variant(srv);
	ASSERT_LE(0, inval_fd)
	{
		TH_LOG("Failed to create socket: %s", strerror(errno));
	}

	/* Tries to bind with zero as addrlen. */
	EXPECT_EQ(-EINVAL, bind_variant_addrlen(inval_fd, srv, 0));

	/* Tries to bind with too small addrlen. */
	EXPECT_EQ(-EINVAL, bind_variant_addrlen(inval_fd, srv,
						get_addrlen(srv, true) - 1));

	/* Tries to bind with minimal addrlen. */
	ret = bind_variant_addrlen(inval_fd, srv, get_addrlen(srv, true));
	if (deny_bind) {
		EXPECT_EQ(-EACCES, ret);
	} else {
		EXPECT_EQ(0, ret)
		{
			TH_LOG("Failed to bind to socket: %s", strerror(errno));
		}
	}
	EXPECT_EQ(0, close(inval_fd));

	/* Starts invalid addrlen tests with connect. */
	inval_fd = socket_variant(srv);
	ASSERT_LE(0, inval_fd);

	/* Tries to connect with zero as addrlen. */
	EXPECT_EQ(-EINVAL, connect_variant_addrlen(inval_fd, srv, 0));

	/* Tries to connect with too small addrlen. */
	EXPECT_EQ(-EINVAL, connect_variant_addrlen(inval_fd, srv,
						   get_addrlen(srv, true) - 1));

	/* Tries to connect with minimal addrlen. */
	ret = connect_variant_addrlen(inval_fd, srv, get_addrlen(srv, true));
	if (srv->protocol.domain == AF_UNIX) {
		EXPECT_EQ(-EINVAL, ret);
	} else if (deny_connect) {
		EXPECT_EQ(-EACCES, ret);
	} else if (srv->protocol.type == SOCK_STREAM) {
		/* No listening server, whatever the value of deny_bind. */
		EXPECT_EQ(-ECONNREFUSED, ret);
	} else {
		EXPECT_EQ(0, ret)
		{
			TH_LOG("Failed to connect to socket: %s",
			       strerror(errno));
		}
	}
	EXPECT_EQ(0, close(inval_fd));

	/* Starts connection tests. */
	bind_fd = socket_variant(srv);
	ASSERT_LE(0, bind_fd);

	ret = bind_variant(bind_fd, srv);
	if (deny_bind) {
		EXPECT_EQ(-EACCES, ret);
	} else {
		EXPECT_EQ(0, ret);

		/* Creates a listening socket. */
		if (srv->protocol.type == SOCK_STREAM)
			EXPECT_EQ(0, listen(bind_fd, backlog));
	}

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int connect_fd, ret;

		/* Closes listening socket for the child. */
		EXPECT_EQ(0, close(bind_fd));

		/* Starts connection tests. */
		connect_fd = socket_variant(srv);
		ASSERT_LE(0, connect_fd);
		ret = connect_variant(connect_fd, srv);
		if (deny_connect) {
			EXPECT_EQ(-EACCES, ret);
		} else if (deny_bind) {
			/* No listening server. */
			EXPECT_EQ(-ECONNREFUSED, ret);
		} else {
			EXPECT_EQ(0, ret);
			EXPECT_EQ(1, write(connect_fd, ".", 1));
		}

		EXPECT_EQ(0, close(connect_fd));
		_exit(_metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
		return;
	}

	/* Accepts connection from the child. */
	client_fd = bind_fd;
	if (!deny_bind && !deny_connect) {
		if (srv->protocol.type == SOCK_STREAM) {
			client_fd = accept(bind_fd, NULL, 0);
			ASSERT_LE(0, client_fd);
		}

		EXPECT_EQ(1, read(client_fd, &buf, 1));
		EXPECT_EQ('.', buf);
	}

	EXPECT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(1, WIFEXITED(status));
	EXPECT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

	/* Closes connection, if any. */
	if (client_fd != bind_fd)
		EXPECT_LE(0, close(client_fd));

	/* Closes listening socket. */
	EXPECT_EQ(0, close(bind_fd));
}

TEST_F(protocol, bind)
{
	if (variant->sandbox == TCP_SANDBOX) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
					      LANDLOCK_ACCESS_NET_CONNECT_TCP,
		};
		const struct landlock_net_port_attr tcp_bind_connect_p0 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
					  LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = self->srv0.port,
		};
		const struct landlock_net_port_attr tcp_connect_p1 = {
			.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = self->srv1.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows connect and bind for the first port.  */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect_p0, 0));

		/* Allows connect and denies bind for the second port. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_connect_p1, 0));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	/* Binds a socket to the first port. */
	test_bind_and_connect(_metadata, &self->srv0, false, false);

	/* Binds a socket to the second port. */
	test_bind_and_connect(_metadata, &self->srv1,
			      is_restricted(&variant->prot, variant->sandbox),
			      false);

	/* Binds a socket to the third port. */
	test_bind_and_connect(_metadata, &self->srv2,
			      is_restricted(&variant->prot, variant->sandbox),
			      is_restricted(&variant->prot, variant->sandbox));
}

TEST_F(protocol, connect)
{
	if (variant->sandbox == TCP_SANDBOX) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
					      LANDLOCK_ACCESS_NET_CONNECT_TCP,
		};
		const struct landlock_net_port_attr tcp_bind_connect_p0 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
					  LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = self->srv0.port,
		};
		const struct landlock_net_port_attr tcp_bind_p1 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
			.port = self->srv1.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows connect and bind for the first port. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect_p0, 0));

		/* Allows bind and denies connect for the second port. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_p1, 0));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	test_bind_and_connect(_metadata, &self->srv0, false, false);

	test_bind_and_connect(_metadata, &self->srv1, false,
			      is_restricted(&variant->prot, variant->sandbox));

	test_bind_and_connect(_metadata, &self->srv2,
			      is_restricted(&variant->prot, variant->sandbox),
			      is_restricted(&variant->prot, variant->sandbox));
}

TEST_F(protocol, bind_unspec)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP,
	};
	const struct landlock_net_port_attr tcp_bind = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = self->srv0.port,
	};
	int bind_fd, ret;

	if (variant->sandbox == TCP_SANDBOX) {
		const int ruleset_fd = landlock_create_ruleset(
			&ruleset_attr, sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows bind. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);

	/* Allowed bind on AF_UNSPEC/INADDR_ANY. */
	ret = bind_variant(bind_fd, &self->unspec_any0);
	if (variant->prot.domain == AF_INET) {
		EXPECT_EQ(0, ret)
		{
			TH_LOG("Failed to bind to unspec/any socket: %s",
			       strerror(errno));
		}
	} else {
		EXPECT_EQ(-EINVAL, ret);
	}
	EXPECT_EQ(0, close(bind_fd));

	if (variant->sandbox == TCP_SANDBOX) {
		const int ruleset_fd = landlock_create_ruleset(
			&ruleset_attr, sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Denies bind. */
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);

	/* Denied bind on AF_UNSPEC/INADDR_ANY. */
	ret = bind_variant(bind_fd, &self->unspec_any0);
	if (variant->prot.domain == AF_INET) {
		if (is_restricted(&variant->prot, variant->sandbox)) {
			EXPECT_EQ(-EACCES, ret);
		} else {
			EXPECT_EQ(0, ret);
		}
	} else {
		EXPECT_EQ(-EINVAL, ret);
	}
	EXPECT_EQ(0, close(bind_fd));

	/* Checks bind with AF_UNSPEC and the loopback address. */
	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);
	ret = bind_variant(bind_fd, &self->unspec_srv0);
	if (variant->prot.domain == AF_INET) {
		EXPECT_EQ(-EAFNOSUPPORT, ret);
	} else {
		EXPECT_EQ(-EINVAL, ret)
		{
			TH_LOG("Wrong bind error: %s", strerror(errno));
		}
	}
	EXPECT_EQ(0, close(bind_fd));
}

TEST_F(protocol, connect_unspec)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};
	const struct landlock_net_port_attr tcp_connect = {
		.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
		.port = self->srv0.port,
	};
	int bind_fd, client_fd, status;
	pid_t child;

	/* Specific connection tests. */
	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);
	EXPECT_EQ(0, bind_variant(bind_fd, &self->srv0));
	if (self->srv0.protocol.type == SOCK_STREAM)
		EXPECT_EQ(0, listen(bind_fd, backlog));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int connect_fd, ret;

		/* Closes listening socket for the child. */
		EXPECT_EQ(0, close(bind_fd));

		connect_fd = socket_variant(&self->srv0);
		ASSERT_LE(0, connect_fd);
		EXPECT_EQ(0, connect_variant(connect_fd, &self->srv0));

		/* Tries to connect again, or set peer. */
		ret = connect_variant(connect_fd, &self->srv0);
		if (self->srv0.protocol.type == SOCK_STREAM) {
			EXPECT_EQ(-EISCONN, ret);
		} else {
			EXPECT_EQ(0, ret);
		}

		if (variant->sandbox == TCP_SANDBOX) {
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_attr, sizeof(ruleset_attr), 0);
			ASSERT_LE(0, ruleset_fd);

			/* Allows connect. */
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &tcp_connect, 0));
			enforce_ruleset(_metadata, ruleset_fd);
			EXPECT_EQ(0, close(ruleset_fd));
		}

		/* Disconnects already connected socket, or set peer. */
		ret = connect_variant(connect_fd, &self->unspec_any0);
		if (self->srv0.protocol.domain == AF_UNIX &&
		    self->srv0.protocol.type == SOCK_STREAM) {
			EXPECT_EQ(-EINVAL, ret);
		} else {
			EXPECT_EQ(0, ret);
		}

		/* Tries to reconnect, or set peer. */
		ret = connect_variant(connect_fd, &self->srv0);
		if (self->srv0.protocol.domain == AF_UNIX &&
		    self->srv0.protocol.type == SOCK_STREAM) {
			EXPECT_EQ(-EISCONN, ret);
		} else {
			EXPECT_EQ(0, ret);
		}

		if (variant->sandbox == TCP_SANDBOX) {
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_attr, sizeof(ruleset_attr), 0);
			ASSERT_LE(0, ruleset_fd);

			/* Denies connect. */
			enforce_ruleset(_metadata, ruleset_fd);
			EXPECT_EQ(0, close(ruleset_fd));
		}

		ret = connect_variant(connect_fd, &self->unspec_any0);
		if (self->srv0.protocol.domain == AF_UNIX &&
		    self->srv0.protocol.type == SOCK_STREAM) {
			EXPECT_EQ(-EINVAL, ret);
		} else {
			/* Always allowed to disconnect. */
			EXPECT_EQ(0, ret);
		}

		EXPECT_EQ(0, close(connect_fd));
		_exit(_metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
		return;
	}

	client_fd = bind_fd;
	if (self->srv0.protocol.type == SOCK_STREAM) {
		client_fd = accept(bind_fd, NULL, 0);
		ASSERT_LE(0, client_fd);
	}

	EXPECT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(1, WIFEXITED(status));
	EXPECT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

	/* Closes connection, if any. */
	if (client_fd != bind_fd)
		EXPECT_LE(0, close(client_fd));

	/* Closes listening socket. */
	EXPECT_EQ(0, close(bind_fd));
}

FIXTURE(ipv4)
{
	struct service_fixture srv0, srv1;
};

FIXTURE_VARIANT(ipv4)
{
	const enum sandbox_type sandbox;
	const int type;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ipv4, no_sandbox_with_tcp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.type = SOCK_STREAM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ipv4, tcp_sandbox_with_tcp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.type = SOCK_STREAM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ipv4, no_sandbox_with_udp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.type = SOCK_DGRAM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ipv4, tcp_sandbox_with_udp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.type = SOCK_DGRAM,
};

FIXTURE_SETUP(ipv4)
{
	const struct protocol_variant prot = {
		.domain = AF_INET,
		.type = variant->type,
	};

	disable_caps(_metadata);

	set_service(&self->srv0, prot, 0);
	set_service(&self->srv1, prot, 1);

	setup_loopback(_metadata);
};

FIXTURE_TEARDOWN(ipv4)
{
}

TEST_F(ipv4, from_unix_to_inet)
{
	int unix_stream_fd, unix_dgram_fd;

	if (variant->sandbox == TCP_SANDBOX) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
					      LANDLOCK_ACCESS_NET_CONNECT_TCP,
		};
		const struct landlock_net_port_attr tcp_bind_connect_p0 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
					  LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = self->srv0.port,
		};
		int ruleset_fd;

		/* Denies connect and bind to check errno value. */
		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows connect and bind for srv0.  */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect_p0, 0));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	unix_stream_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	ASSERT_LE(0, unix_stream_fd);

	unix_dgram_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	ASSERT_LE(0, unix_dgram_fd);

	/* Checks unix stream bind and connect for srv0. */
	EXPECT_EQ(-EINVAL, bind_variant(unix_stream_fd, &self->srv0));
	EXPECT_EQ(-EINVAL, connect_variant(unix_stream_fd, &self->srv0));

	/* Checks unix stream bind and connect for srv1. */
	EXPECT_EQ(-EINVAL, bind_variant(unix_stream_fd, &self->srv1))
	{
		TH_LOG("Wrong bind error: %s", strerror(errno));
	}
	EXPECT_EQ(-EINVAL, connect_variant(unix_stream_fd, &self->srv1));

	/* Checks unix datagram bind and connect for srv0. */
	EXPECT_EQ(-EINVAL, bind_variant(unix_dgram_fd, &self->srv0));
	EXPECT_EQ(-EINVAL, connect_variant(unix_dgram_fd, &self->srv0));

	/* Checks unix datagram bind and connect for srv1. */
	EXPECT_EQ(-EINVAL, bind_variant(unix_dgram_fd, &self->srv1));
	EXPECT_EQ(-EINVAL, connect_variant(unix_dgram_fd, &self->srv1));
}

FIXTURE(tcp_layers)
{
	struct service_fixture srv0, srv1;
};

FIXTURE_VARIANT(tcp_layers)
{
	const size_t num_layers;
	const int domain;
};

FIXTURE_SETUP(tcp_layers)
{
	const struct protocol_variant prot = {
		.domain = variant->domain,
		.type = SOCK_STREAM,
	};

	disable_caps(_metadata);

	ASSERT_EQ(0, set_service(&self->srv0, prot, 0));
	ASSERT_EQ(0, set_service(&self->srv1, prot, 1));

	setup_loopback(_metadata);
};

FIXTURE_TEARDOWN(tcp_layers)
{
}

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, no_sandbox_with_ipv4) {
	/* clang-format on */
	.domain = AF_INET,
	.num_layers = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, one_sandbox_with_ipv4) {
	/* clang-format on */
	.domain = AF_INET,
	.num_layers = 1,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, two_sandboxes_with_ipv4) {
	/* clang-format on */
	.domain = AF_INET,
	.num_layers = 2,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, three_sandboxes_with_ipv4) {
	/* clang-format on */
	.domain = AF_INET,
	.num_layers = 3,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, no_sandbox_with_ipv6) {
	/* clang-format on */
	.domain = AF_INET6,
	.num_layers = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, one_sandbox_with_ipv6) {
	/* clang-format on */
	.domain = AF_INET6,
	.num_layers = 1,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, two_sandboxes_with_ipv6) {
	/* clang-format on */
	.domain = AF_INET6,
	.num_layers = 2,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(tcp_layers, three_sandboxes_with_ipv6) {
	/* clang-format on */
	.domain = AF_INET6,
	.num_layers = 3,
};

TEST_F(tcp_layers, ruleset_overlap)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
				      LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};
	const struct landlock_net_port_attr tcp_bind = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = self->srv0.port,
	};
	const struct landlock_net_port_attr tcp_bind_connect = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
				  LANDLOCK_ACCESS_NET_CONNECT_TCP,
		.port = self->srv0.port,
	};

	if (variant->num_layers >= 1) {
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows bind. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind, 0));
		/* Also allows bind, but allows connect too. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	if (variant->num_layers >= 2) {
		int ruleset_fd;

		/* Creates another ruleset layer. */
		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Only allows bind. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	if (variant->num_layers >= 3) {
		int ruleset_fd;

		/* Creates another ruleset layer. */
		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Try to allow bind and connect. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	/*
	 * Forbids to connect to the socket because only one ruleset layer
	 * allows connect.
	 */
	test_bind_and_connect(_metadata, &self->srv0, false,
			      variant->num_layers >= 2);
}

TEST_F(tcp_layers, ruleset_expand)
{
	if (variant->num_layers >= 1) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP,
		};
		/* Allows bind for srv0. */
		const struct landlock_net_port_attr bind_srv0 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
			.port = self->srv0.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &bind_srv0, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	if (variant->num_layers >= 2) {
		/* Expands network mask with connect action. */
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
					      LANDLOCK_ACCESS_NET_CONNECT_TCP,
		};
		/* Allows bind for srv0 and connect to srv0. */
		const struct landlock_net_port_attr tcp_bind_connect_p0 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
					  LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = self->srv0.port,
		};
		/* Try to allow bind for srv1. */
		const struct landlock_net_port_attr tcp_bind_p1 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
			.port = self->srv1.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect_p0, 0));
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_p1, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	if (variant->num_layers >= 3) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
					      LANDLOCK_ACCESS_NET_CONNECT_TCP,
		};
		/* Allows connect to srv0, without bind rule. */
		const struct landlock_net_port_attr tcp_bind_p0 = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
			.port = self->srv0.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_p0, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	test_bind_and_connect(_metadata, &self->srv0, false,
			      variant->num_layers >= 3);

	test_bind_and_connect(_metadata, &self->srv1, variant->num_layers >= 1,
			      variant->num_layers >= 2);
}

/* clang-format off */
FIXTURE(mini) {};
/* clang-format on */

FIXTURE_SETUP(mini)
{
	disable_caps(_metadata);

	setup_loopback(_metadata);
};

FIXTURE_TEARDOWN(mini)
{
}

/* clang-format off */

#define ACCESS_LAST LANDLOCK_ACCESS_NET_CONNECT_TCP

#define ACCESS_ALL ( \
	LANDLOCK_ACCESS_NET_BIND_TCP | \
	LANDLOCK_ACCESS_NET_CONNECT_TCP)

/* clang-format on */

TEST_F(mini, network_access_rights)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = ACCESS_ALL,
	};
	struct landlock_net_port_attr net_port = {
		.port = sock_port_start,
	};
	int ruleset_fd;
	__u64 access;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	for (access = 1; access <= ACCESS_LAST; access <<= 1) {
		net_port.allowed_access = access;
		EXPECT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &net_port, 0))
		{
			TH_LOG("Failed to add rule with access 0x%llx: %s",
			       access, strerror(errno));
		}
	}
	EXPECT_EQ(0, close(ruleset_fd));
}

/* Checks invalid attribute, out of landlock network access range. */
TEST_F(mini, ruleset_with_unknown_access)
{
	__u64 access_mask;

	for (access_mask = 1ULL << 63; access_mask != ACCESS_LAST;
	     access_mask >>= 1) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = access_mask,
		};

		EXPECT_EQ(-1, landlock_create_ruleset(&ruleset_attr,
						      sizeof(ruleset_attr), 0));
		EXPECT_EQ(EINVAL, errno);
	}
}

TEST_F(mini, rule_with_unknown_access)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = ACCESS_ALL,
	};
	struct landlock_net_port_attr net_port = {
		.port = sock_port_start,
	};
	int ruleset_fd;
	__u64 access;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	for (access = 1ULL << 63; access != ACCESS_LAST; access >>= 1) {
		net_port.allowed_access = access;
		EXPECT_EQ(-1,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &net_port, 0));
		EXPECT_EQ(EINVAL, errno);
	}
	EXPECT_EQ(0, close(ruleset_fd));
}

TEST_F(mini, rule_with_unhandled_access)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP,
	};
	struct landlock_net_port_attr net_port = {
		.port = sock_port_start,
	};
	int ruleset_fd;
	__u64 access;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	for (access = 1; access > 0; access <<= 1) {
		int err;

		net_port.allowed_access = access;
		err = landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					&net_port, 0);
		if (access == ruleset_attr.handled_access_net) {
			EXPECT_EQ(0, err);
		} else {
			EXPECT_EQ(-1, err);
			EXPECT_EQ(EINVAL, errno);
		}
	}

	EXPECT_EQ(0, close(ruleset_fd));
}

TEST_F(mini, inval)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP
	};
	const struct landlock_net_port_attr tcp_bind_connect = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
				  LANDLOCK_ACCESS_NET_CONNECT_TCP,
		.port = sock_port_start,
	};
	const struct landlock_net_port_attr tcp_denied = {
		.allowed_access = 0,
		.port = sock_port_start,
	};
	const struct landlock_net_port_attr tcp_bind = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = sock_port_start,
	};
	int ruleset_fd;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	/* Checks unhandled allowed_access. */
	EXPECT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					&tcp_bind_connect, 0));
	EXPECT_EQ(EINVAL, errno);

	/* Checks zero access value. */
	EXPECT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					&tcp_denied, 0));
	EXPECT_EQ(ENOMSG, errno);

	/* Adds with legitimate values. */
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &tcp_bind, 0));
}

TEST_F(mini, tcp_port_overflow)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
				      LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};
	const struct landlock_net_port_attr port_max_bind = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = UINT16_MAX,
	};
	const struct landlock_net_port_attr port_max_connect = {
		.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
		.port = UINT16_MAX,
	};
	const struct landlock_net_port_attr port_overflow1 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = UINT16_MAX + 1,
	};
	const struct landlock_net_port_attr port_overflow2 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = UINT16_MAX + 2,
	};
	const struct landlock_net_port_attr port_overflow3 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = UINT32_MAX + 1UL,
	};
	const struct landlock_net_port_attr port_overflow4 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = UINT32_MAX + 2UL,
	};
	const struct protocol_variant ipv4_tcp = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	};
	struct service_fixture srv_denied, srv_max_allowed;
	int ruleset_fd;

	ASSERT_EQ(0, set_service(&srv_denied, ipv4_tcp, 0));

	/* Be careful to avoid port inconsistencies. */
	srv_max_allowed = srv_denied;
	srv_max_allowed.port = port_max_bind.port;
	srv_max_allowed.ipv4_addr.sin_port = htons(port_max_bind.port);

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &port_max_bind, 0));

	EXPECT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					&port_overflow1, 0));
	EXPECT_EQ(EINVAL, errno);

	EXPECT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					&port_overflow2, 0));
	EXPECT_EQ(EINVAL, errno);

	EXPECT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					&port_overflow3, 0));
	EXPECT_EQ(EINVAL, errno);

	/* Interleaves with invalid rule additions. */
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &port_max_connect, 0));

	EXPECT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					&port_overflow4, 0));
	EXPECT_EQ(EINVAL, errno);

	enforce_ruleset(_metadata, ruleset_fd);

	test_bind_and_connect(_metadata, &srv_denied, true, true);
	test_bind_and_connect(_metadata, &srv_max_allowed, false, false);
}

FIXTURE(ipv4_tcp)
{
	struct service_fixture srv0, srv1;
};

FIXTURE_SETUP(ipv4_tcp)
{
	const struct protocol_variant ipv4_tcp = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	};

	disable_caps(_metadata);

	ASSERT_EQ(0, set_service(&self->srv0, ipv4_tcp, 0));
	ASSERT_EQ(0, set_service(&self->srv1, ipv4_tcp, 1));

	setup_loopback(_metadata);
};

FIXTURE_TEARDOWN(ipv4_tcp)
{
}

TEST_F(ipv4_tcp, port_endianness)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
				      LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};
	const struct landlock_net_port_attr bind_host_endian_p0 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		/* Host port format. */
		.port = self->srv0.port,
	};
	const struct landlock_net_port_attr connect_big_endian_p0 = {
		.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
		/* Big endian port format. */
		.port = htons(self->srv0.port),
	};
	const struct landlock_net_port_attr bind_connect_host_endian_p1 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
				  LANDLOCK_ACCESS_NET_CONNECT_TCP,
		/* Host port format. */
		.port = self->srv1.port,
	};
	const unsigned int one = 1;
	const char little_endian = *(const char *)&one;
	int ruleset_fd;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &bind_host_endian_p0, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &connect_big_endian_p0, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &bind_connect_host_endian_p1, 0));
	enforce_ruleset(_metadata, ruleset_fd);

	/* No restriction for big endinan CPU. */
	test_bind_and_connect(_metadata, &self->srv0, false, little_endian);

	/* No restriction for any CPU. */
	test_bind_and_connect(_metadata, &self->srv1, false, false);
}

TEST_F(ipv4_tcp, with_fs)
{
	const struct landlock_ruleset_attr ruleset_attr_fs_net = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP,
	};
	struct landlock_path_beneath_attr path_beneath = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_DIR,
		.parent_fd = -1,
	};
	struct landlock_net_port_attr tcp_bind = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = self->srv0.port,
	};
	int ruleset_fd, bind_fd, dir_fd;

	/* Creates ruleset both for filesystem and network access. */
	ruleset_fd = landlock_create_ruleset(&ruleset_attr_fs_net,
					     sizeof(ruleset_attr_fs_net), 0);
	ASSERT_LE(0, ruleset_fd);

	/* Adds a filesystem rule. */
	path_beneath.parent_fd = open("/dev", O_PATH | O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				       &path_beneath, 0));
	EXPECT_EQ(0, close(path_beneath.parent_fd));

	/* Adds a network rule. */
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &tcp_bind, 0));

	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* Tests file access. */
	dir_fd = open("/dev", O_RDONLY);
	EXPECT_LE(0, dir_fd);
	EXPECT_EQ(0, close(dir_fd));

	dir_fd = open("/", O_RDONLY);
	EXPECT_EQ(-1, dir_fd);
	EXPECT_EQ(EACCES, errno);

	/* Tests port binding. */
	bind_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	ASSERT_LE(0, bind_fd);
	EXPECT_EQ(0, bind_variant(bind_fd, &self->srv0));
	EXPECT_EQ(0, close(bind_fd));

	bind_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	ASSERT_LE(0, bind_fd);
	EXPECT_EQ(-EACCES, bind_variant(bind_fd, &self->srv1));
}

FIXTURE(port_specific)
{
	struct service_fixture srv0;
};

FIXTURE_VARIANT(port_specific)
{
	const enum sandbox_type sandbox;
	const struct protocol_variant prot;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(port_specific, no_sandbox_with_ipv4) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(port_specific, sandbox_with_ipv4) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(port_specific, no_sandbox_with_ipv6) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(port_specific, sandbox_with_ipv6) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
	},
};

FIXTURE_SETUP(port_specific)
{
	disable_caps(_metadata);

	ASSERT_EQ(0, set_service(&self->srv0, variant->prot, 0));

	setup_loopback(_metadata);
};

FIXTURE_TEARDOWN(port_specific)
{
}

TEST_F(port_specific, bind_connect_zero)
{
	int bind_fd, connect_fd, ret;
	uint16_t port;

	/* Adds a rule layer with bind and connect actions. */
	if (variant->sandbox == TCP_SANDBOX) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
					      LANDLOCK_ACCESS_NET_CONNECT_TCP
		};
		const struct landlock_net_port_attr tcp_bind_connect_zero = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
					  LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = 0,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Checks zero port value on bind and connect actions. */
		EXPECT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect_zero, 0));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);

	connect_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, connect_fd);

	/* Sets address port to 0 for both protocol families. */
	set_port(&self->srv0, 0);
	/*
	 * Binds on port 0, which selects a random port within
	 * ip_local_port_range.
	 */
	ret = bind_variant(bind_fd, &self->srv0);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(0, listen(bind_fd, backlog));

	/* Connects on port 0. */
	ret = connect_variant(connect_fd, &self->srv0);
	EXPECT_EQ(-ECONNREFUSED, ret);

	/* Sets binded port for both protocol families. */
	port = get_binded_port(bind_fd, &variant->prot);
	EXPECT_NE(0, port);
	set_port(&self->srv0, port);
	/* Connects on the binded port. */
	ret = connect_variant(connect_fd, &self->srv0);
	if (is_restricted(&variant->prot, variant->sandbox)) {
		/* Denied by Landlock. */
		EXPECT_EQ(-EACCES, ret);
	} else {
		EXPECT_EQ(0, ret);
	}

	EXPECT_EQ(0, close(connect_fd));
	EXPECT_EQ(0, close(bind_fd));
}

TEST_F(port_specific, bind_connect_1023)
{
	int bind_fd, connect_fd, ret;

	/* Adds a rule layer with bind and connect actions. */
	if (variant->sandbox == TCP_SANDBOX) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
					      LANDLOCK_ACCESS_NET_CONNECT_TCP
		};
		/* A rule with port value less than 1024. */
		const struct landlock_net_port_attr tcp_bind_connect_low_range = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
					  LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = 1023,
		};
		/* A rule with 1024 port. */
		const struct landlock_net_port_attr tcp_bind_connect = {
			.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
					  LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = 1024,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect_low_range, 0));
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &tcp_bind_connect, 0));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);

	connect_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, connect_fd);

	/* Sets address port to 1023 for both protocol families. */
	set_port(&self->srv0, 1023);
	/* Binds on port 1023. */
	ret = bind_variant(bind_fd, &self->srv0);
	/* Denied by the system. */
	EXPECT_EQ(-EACCES, ret);

	/* Binds on port 1023. */
	set_cap(_metadata, CAP_NET_BIND_SERVICE);
	ret = bind_variant(bind_fd, &self->srv0);
	clear_cap(_metadata, CAP_NET_BIND_SERVICE);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, listen(bind_fd, backlog));

	/* Connects on the binded port 1023. */
	ret = connect_variant(connect_fd, &self->srv0);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(0, close(connect_fd));
	EXPECT_EQ(0, close(bind_fd));

	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);

	connect_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, connect_fd);

	/* Sets address port to 1024 for both protocol families. */
	set_port(&self->srv0, 1024);
	/* Binds on port 1024. */
	ret = bind_variant(bind_fd, &self->srv0);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, listen(bind_fd, backlog));

	/* Connects on the binded port 1024. */
	ret = connect_variant(connect_fd, &self->srv0);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(0, close(connect_fd));
	EXPECT_EQ(0, close(bind_fd));
}

TEST_HARNESS_MAIN
