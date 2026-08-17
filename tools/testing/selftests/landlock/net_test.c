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

#include "audit.h"
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
	UDP_SANDBOX,
};

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
		set_unix_address(srv, index);
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

static bool prot_is_tcp(const struct protocol_variant *const prot)
{
	return (prot->domain == AF_INET || prot->domain == AF_INET6) &&
	       prot->type == SOCK_STREAM &&
	       (prot->protocol == IPPROTO_TCP || prot->protocol == IPPROTO_IP);
}

static bool prot_is_udp(const struct protocol_variant *const prot)
{
	return (prot->domain == AF_INET || prot->domain == AF_INET6) &&
	       prot->type == SOCK_DGRAM &&
	       (prot->protocol == IPPROTO_UDP || prot->protocol == IPPROTO_IP);
}

static bool is_restricted(const struct protocol_variant *const prot,
			  const enum sandbox_type sandbox)
{
	if (sandbox == TCP_SANDBOX)
		return prot_is_tcp(prot);
	else if (sandbox == UDP_SANDBOX)
		return prot_is_udp(prot);
	return false;
}

static int socket_variant(const struct service_fixture *const srv)
{
	/* Arbitrary value just to not block other tests indefinitely. */
	const struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 100000,
	};
	int sockfd;
	int ret;

	sockfd = socket(srv->protocol.domain, srv->protocol.type | SOCK_CLOEXEC,
			srv->protocol.protocol);
	if (sockfd < 0)
		return -errno;

	ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			 sizeof(timeout));
	if (ret != 0) {
		ret = -errno;
		close(sockfd);
		return ret;
	}
	ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
			 sizeof(timeout));
	if (ret != 0) {
		ret = -errno;
		close(sockfd);
		return ret;
	}
	return sockfd;
}

#ifndef SIN6_LEN_RFC2133
#define SIN6_LEN_RFC2133 24
#endif

static socklen_t get_addrlen(const struct service_fixture *const srv,
			     const bool minimal)
{
	switch (srv->protocol.domain) {
	case AF_UNSPEC:
		if (minimal)
			return sizeof(sa_family_t);
		return sizeof(struct sockaddr_storage);

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

static int sendto_variant_addrlen(const int sock_fd,
				  const struct service_fixture *const srv,
				  const socklen_t addrlen, void *buf,
				  size_t len, size_t flags)
{
	const struct sockaddr *dst = NULL;
	ssize_t ret;

	/*
	 * We never want our processes to be killed by SIGPIPE: we check return
	 * codes and errno, so that we have actual error messages.
	 */
	flags |= MSG_NOSIGNAL;

	if (srv != NULL) {
		switch (srv->protocol.domain) {
		case AF_UNSPEC:
		case AF_INET:
			dst = (const struct sockaddr *)&srv->ipv4_addr;
			break;

		case AF_INET6:
			dst = (const struct sockaddr *)&srv->ipv6_addr;
			break;

		case AF_UNIX:
			dst = (const struct sockaddr *)&srv->unix_addr;
			break;

		default:
			errno = EAFNOSUPPORT;
			return -errno;
		}
	}

	ret = sendto(sock_fd, buf, len, flags, dst, addrlen);
	if (ret < 0)
		return -errno;

	/* errno is not set in cases of partial writes. */
	if (ret != len)
		return -EINTR;

	return 0;
}

static int sendto_variant(const int sock_fd,
			  const struct service_fixture *const srv, void *buf,
			  size_t len, size_t flags)
{
	socklen_t addrlen = 0;

	if (srv != NULL)
		addrlen = get_addrlen(srv, false);

	return sendto_variant_addrlen(sock_fd, srv, addrlen, buf, len, flags);
}

static int test_sendmsg(struct __test_metadata *const _metadata,
			const struct protocol_variant *prot, int client_fd,
			int server_fd, const struct service_fixture *srv,
			bool bind_denied, bool send_denied)
{
	int ret;
	socklen_t opt_len;
	int sock_type;
	int addr_family;
	struct sockaddr_storage peer_addr = { 0 };
	bool has_remote_port;
	bool needs_autobind;
	char read_buf[1] = { 0 };

	/*
	 * Prepare the test by inspecting the socket type and whether it has a
	 * local/remote address set (all of which determine the expected
	 * outcomes).
	 */
	opt_len = sizeof(sock_type);
	ASSERT_EQ(0, getsockopt(client_fd, SOL_SOCKET, SO_TYPE, &sock_type,
				&opt_len));
	opt_len = sizeof(addr_family);
	ASSERT_EQ(0, getsockopt(client_fd, SOL_SOCKET, SO_DOMAIN, &addr_family,
				&opt_len));
	opt_len = sizeof(peer_addr);
	has_remote_port = (getpeername(client_fd, (struct sockaddr *)&peer_addr,
				       &opt_len) == 0);
	needs_autobind = (addr_family == AF_INET || addr_family == AF_INET6) &&
			 get_binded_port(client_fd, prot) == 0;

	/* First, check error code with truncated explicit address. */
	if (srv != NULL) {
		ret = sendto_variant_addrlen(
			client_fd, srv, get_addrlen(srv, true) - 1, "A", 1, 0);
		if (sock_type == SOCK_STREAM && !has_remote_port) {
			EXPECT_EQ(-EPIPE, ret)
			{
				return -1;
			}
		} else if (bind_denied && needs_autobind) {
			EXPECT_EQ(-EACCES, ret)
			{
				return -1;
			}
		} else {
			EXPECT_EQ(-EINVAL, ret)
			{
				return -1;
			}
		}
	}

	/* With or without explicit destination address (srv can be NULL). */
	ret = sendto_variant(client_fd, srv, "B", 1, 0);
	if (sock_type == SOCK_STREAM && !has_remote_port) {
		EXPECT_EQ(-EPIPE, ret)
		{
			return -1;
		}
	} else if ((send_denied && srv != NULL) ||
		   (bind_denied && needs_autobind)) {
		ASSERT_EQ(-EACCES, ret)
		{
			return -1;
		}
	} else if (srv == NULL && !has_remote_port) {
		if (addr_family == AF_UNIX) {
			ASSERT_EQ(-ENOTCONN, ret)
			{
				return -1;
			}
		} else if (sock_type == SOCK_STREAM) {
			ASSERT_EQ(-EPIPE, ret)
			{
				return -1;
			}
		} else {
			ASSERT_EQ(-EDESTADDRREQ, ret)
			{
				return -1;
			}
		}
	} else {
		ASSERT_EQ(0, ret);
		ASSERT_EQ(1, recv(server_fd, read_buf, 1, 0));
		ASSERT_EQ(read_buf[0], 'B')
		{
			return -1;
		}
	}

	return 0;
}

FIXTURE(protocol)
{
	struct service_fixture srv0, srv1, srv2;
	struct service_fixture unspec_any0, unspec_srv0, unspec_srv1;
};

FIXTURE_VARIANT(protocol)
{
	const enum sandbox_type sandbox;
	const struct protocol_variant prot;
};

FIXTURE_SETUP(protocol)
{
	struct protocol_variant prot_unspec = variant->prot;

	prot_unspec.domain = AF_UNSPEC;

	disable_caps(_metadata);

	ASSERT_EQ(0, set_service(&self->srv0, variant->prot, 0));
	ASSERT_EQ(0, set_service(&self->srv1, variant->prot, 1));
	ASSERT_EQ(0, set_service(&self->srv2, variant->prot, 2));

	ASSERT_EQ(0, set_service(&self->unspec_srv0, prot_unspec, 0));
	ASSERT_EQ(0, set_service(&self->unspec_srv1, prot_unspec, 1));

	ASSERT_EQ(0, set_service(&self->unspec_any0, prot_unspec, 0));
	self->unspec_any0.ipv4_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	setup_loopback(_metadata);
};

FIXTURE_TEARDOWN(protocol)
{
}

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv4_tcp1) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
		/* IPPROTO_IP == 0 */
		.protocol = IPPROTO_IP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv4_tcp2) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_TCP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv4_mptcp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_MPTCP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv6_tcp1) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		/* IPPROTO_IP == 0 */
		.protocol = IPPROTO_IP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv6_tcp2) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_TCP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, no_sandbox_with_ipv6_mptcp) {
	/* clang-format on */
	.sandbox = NO_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_MPTCP,
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
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv4_tcp1) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
		/* IPPROTO_IP == 0 */
		.protocol = IPPROTO_IP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv4_tcp2) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_TCP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv4_mptcp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_MPTCP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv6_tcp1) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		/* IPPROTO_IP == 0 */
		.protocol = IPPROTO_IP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv6_tcp2) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_TCP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, tcp_sandbox_with_ipv6_mptcp) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_MPTCP,
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

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_ipv4_udp1) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_DGRAM,
		.protocol = IPPROTO_UDP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_ipv4_udp2) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_DGRAM,
		/* IPPROTO_IP == 0 */
		.protocol = IPPROTO_IP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_ipv6_udp1) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_DGRAM,
		.protocol = IPPROTO_UDP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_ipv6_udp2) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_DGRAM,
		/* IPPROTO_IP == 0 */
		.protocol = IPPROTO_IP,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_ipv4_tcp) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_ipv6_tcp) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_unix_stream) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_UNIX,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(protocol, udp_sandbox_with_unix_datagram) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
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
		} else if (deny_bind && srv->protocol.type == SOCK_STREAM) {
			/* No listening server. */
			EXPECT_EQ(-ECONNREFUSED, ret);
		} else {
			EXPECT_EQ(0, ret);
			EXPECT_EQ(1, write(connect_fd, ".", 1));
		}

		EXPECT_EQ(0, close(connect_fd));
		_exit(_metadata->exit_code);
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
	if (variant->sandbox == TCP_SANDBOX ||
	    variant->sandbox == UDP_SANDBOX) {
		const __u64 bind_access =
			(variant->sandbox == TCP_SANDBOX ?
				 LANDLOCK_ACCESS_NET_BIND_TCP :
				 LANDLOCK_ACCESS_NET_BIND_UDP);
		const __u64 conn_access =
			(variant->sandbox == TCP_SANDBOX ?
				 LANDLOCK_ACCESS_NET_CONNECT_TCP :
				 LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = bind_access | conn_access,
		};
		const struct landlock_net_port_attr bind_connect_p0 = {
			.allowed_access = bind_access | conn_access,
			.port = self->srv0.port,
		};
		const struct landlock_net_port_attr connect_p1 = {
			.allowed_access = conn_access,
			.port = self->srv1.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows connect and bind for the first port.  */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &bind_connect_p0, 0));

		/* Allows connect and denies bind for the second port. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &connect_p1, 0));

		/*
		 * For UDP sockets, allows binding to ephemeral ports (required
		 * to connect or send a first datagram)
		 */
		if (variant->sandbox == UDP_SANDBOX) {
			const struct landlock_net_port_attr bind_ephemeral = {
				.allowed_access = bind_access,
				.port = 0,
			};
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &bind_ephemeral, 0));
		}

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
	if (variant->sandbox == TCP_SANDBOX ||
	    variant->sandbox == UDP_SANDBOX) {
		const __u64 bind_access =
			(variant->sandbox == TCP_SANDBOX ?
				 LANDLOCK_ACCESS_NET_BIND_TCP :
				 LANDLOCK_ACCESS_NET_BIND_UDP);
		const __u64 conn_access =
			(variant->sandbox == TCP_SANDBOX ?
				 LANDLOCK_ACCESS_NET_CONNECT_TCP :
				 LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = bind_access | conn_access,
		};
		const struct landlock_net_port_attr bind_connect_p0 = {
			.allowed_access = bind_access | conn_access,
			.port = self->srv0.port,
		};
		const struct landlock_net_port_attr bind_p1 = {
			.allowed_access = bind_access,
			.port = self->srv1.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows connect and bind for the first port. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &bind_connect_p0, 0));

		/* Allows bind and denies connect for the second port. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &bind_p1, 0));

		/*
		 * For UDP sockets, allows binding to ephemeral ports (required
		 * to connect or send a first datagram)
		 */
		if (variant->sandbox == UDP_SANDBOX) {
			const struct landlock_net_port_attr bind_ephemeral = {
				.allowed_access = bind_access,
				.port = 0,
			};
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &bind_ephemeral, 0));
		}

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
	const __u64 bind_access = (variant->sandbox == TCP_SANDBOX ?
					   LANDLOCK_ACCESS_NET_BIND_TCP :
					   LANDLOCK_ACCESS_NET_BIND_UDP);
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = bind_access,
	};
	const struct landlock_net_port_attr rule_bind = {
		.allowed_access = bind_access,
		.port = self->srv0.port,
	};
	int bind_fd, ret;

	if (variant->sandbox == TCP_SANDBOX ||
	    variant->sandbox == UDP_SANDBOX) {
		const int ruleset_fd = landlock_create_ruleset(
			&ruleset_attr, sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Allows bind. */
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &rule_bind, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);

	/* Tries to bind with too small addrlen. */
	EXPECT_EQ(-EINVAL, bind_variant_addrlen(
				   bind_fd, &self->unspec_any0,
				   get_addrlen(&self->unspec_any0, true) - 1));

	/* Allowed bind on AF_UNSPEC/INADDR_ANY. */
	ret = bind_variant(bind_fd, &self->unspec_any0);
	if (variant->prot.domain == AF_INET) {
		EXPECT_EQ(0, ret)
		{
			TH_LOG("Failed to bind to unspec/any socket: %s",
			       strerror(errno));
		}
	} else if (variant->prot.domain == AF_INET6) {
		EXPECT_EQ(-EAFNOSUPPORT, ret);
	} else {
		EXPECT_EQ(-EINVAL, ret);
	}
	EXPECT_EQ(0, close(bind_fd));

	if (variant->sandbox == TCP_SANDBOX ||
	    variant->sandbox == UDP_SANDBOX) {
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
	} else if (variant->prot.domain == AF_INET6) {
		EXPECT_EQ(-EAFNOSUPPORT, ret);
	} else {
		EXPECT_EQ(-EINVAL, ret);
	}
	EXPECT_EQ(0, close(bind_fd));

	/* Checks bind with AF_UNSPEC and the loopback address. */
	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);
	ret = bind_variant(bind_fd, &self->unspec_srv0);
	if (variant->prot.domain == AF_INET ||
	    variant->prot.domain == AF_INET6) {
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
	const __u64 connect_right =
		(variant->sandbox == TCP_SANDBOX ?
			 LANDLOCK_ACCESS_NET_CONNECT_TCP :
			 LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
	const __u64 bind_right = (variant->sandbox == TCP_SANDBOX ?
					  LANDLOCK_ACCESS_NET_BIND_TCP :
					  LANDLOCK_ACCESS_NET_BIND_UDP);
	const struct landlock_ruleset_attr ruleset_conn = {
		.handled_access_net = connect_right,
	};
	const struct landlock_ruleset_attr ruleset_conn_bind = {
		.handled_access_net = connect_right | bind_right,
	};
	const struct landlock_net_port_attr rule_connect = {
		.allowed_access = connect_right,
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

		if (variant->sandbox == TCP_SANDBOX ||
		    variant->sandbox == UDP_SANDBOX) {
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_conn, sizeof(ruleset_conn), 0);
			ASSERT_LE(0, ruleset_fd);

			/* Allows connect. */
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &rule_connect, 0));
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

		if (variant->sandbox == TCP_SANDBOX ||
		    variant->sandbox == UDP_SANDBOX) {
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_conn_bind, sizeof(ruleset_conn_bind),
				0);
			ASSERT_LE(0, ruleset_fd);

			/* Denies connect and bind. */
			enforce_ruleset(_metadata, ruleset_fd);
			EXPECT_EQ(0, close(ruleset_fd));
		}

		/* Try to re-disconnect with a truncated address struct. */
		EXPECT_EQ(-EINVAL,
			  connect_variant_addrlen(
				  connect_fd, &self->unspec_any0,
				  get_addrlen(&self->unspec_any0, true) - 1));

		/*
		 * Re-disconnect, with a minimal sockaddr struct (just a
		 * bare af_family=AF_UNSPEC field).
		 */
		ret = connect_variant_addrlen(connect_fd, &self->unspec_any0,
					      get_addrlen(&self->unspec_any0,
							  true));
		if (self->srv0.protocol.domain == AF_UNIX &&
		    self->srv0.protocol.type == SOCK_STREAM) {
			EXPECT_EQ(-EINVAL, ret);
		} else {
			/* Always allowed to disconnect. */
			EXPECT_EQ(0, ret);
		}

		EXPECT_EQ(0, close(connect_fd));
		_exit(_metadata->exit_code);
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

TEST_F(protocol, sendmsg_stream)
{
	int srv0_fd, tmp_fd, client_fd, res;
	char read_buf[1] = { 0 };

	/*
	 * Simple test for stream sockets: just deny all connect()/
	 * send(explicit addr)/bind(), and make sure we don't interfere with any
	 * operation.
	 */
	if (variant->prot.type != SOCK_STREAM)
		return;

	if (variant->sandbox == UDP_SANDBOX) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net =
				LANDLOCK_ACCESS_NET_BIND_UDP |
				LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,
		};
		const int ruleset_fd = landlock_create_ruleset(
			&ruleset_attr, sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	ASSERT_LE(0, client_fd = socket_variant(&self->srv0));
	ASSERT_LE(0, srv0_fd = socket_variant(&self->srv0));
	ASSERT_EQ(0, bind_variant(srv0_fd, &self->srv0));
	ASSERT_EQ(0, listen(srv0_fd, backlog));

	/* Send on a non-connected socket. */
	res = sendto_variant(client_fd, NULL, "A", 1, 0);
	if (variant->prot.domain == AF_UNIX) {
		EXPECT_EQ(-ENOTCONN, res);
	} else {
		EXPECT_EQ(-EPIPE, res);
	}

	/* Send to a truncated (invalid) address on a non-connected socket. */
	res = sendto_variant_addrlen(client_fd, &self->srv0,
				     get_addrlen(&self->srv0, true) - 1, "B", 1,
				     0);
	if (variant->prot.domain == AF_UNIX) {
		EXPECT_EQ(-EOPNOTSUPP, res);
	} else {
		EXPECT_EQ(-EPIPE, res);
	}

	/* Connect. */
	ASSERT_EQ(0, connect_variant(client_fd, &self->srv0));
	tmp_fd = accept(srv0_fd, NULL, 0);
	ASSERT_LE(0, tmp_fd);
	EXPECT_EQ(0, close(srv0_fd));
	srv0_fd = tmp_fd;

	/* Send without an explicit address. */
	EXPECT_EQ(0, sendto_variant(client_fd, NULL, "C", 1, 0));
	EXPECT_EQ(1, recv(srv0_fd, read_buf, 1, 0))
	{
		TH_LOG("recv() failed: %s", strerror(errno));
	}
	EXPECT_EQ(read_buf[0], 'C');

	/* Send to a truncated (invalid) address. */
	res = sendto_variant_addrlen(client_fd, &self->srv0,
				     get_addrlen(&self->srv0, true) - 1, "D", 1,
				     0);
	if (variant->prot.domain == AF_UNIX) {
		EXPECT_EQ(-EISCONN, res);
	} else {
		ASSERT_EQ(0, res);
		EXPECT_EQ(1, recv(srv0_fd, read_buf, 1, 0))
		{
			TH_LOG("recv() failed: %s", strerror(errno));
		}
		EXPECT_EQ(read_buf[0], 'D');
	}

	/* Send to a valid but different address. */
	res = sendto_variant(client_fd, &self->srv1, "E", 1, 0);
	if (variant->prot.domain == AF_UNIX) {
		EXPECT_EQ(-EISCONN, res);
	} else {
		ASSERT_EQ(0, res);
		EXPECT_EQ(1, recv(srv0_fd, read_buf, 1, 0))
		{
			TH_LOG("recv() failed: %s", strerror(errno));
		}
		EXPECT_EQ(read_buf[0], 'E');
	}

	EXPECT_EQ(0, close(client_fd));
}

TEST_F(protocol, sendmsg_dgram)
{
	const bool restricted = is_restricted(&variant->prot, variant->sandbox);
	int srv0_fd, srv1_fd, client_fd, child, status, res;

	if (variant->prot.type != SOCK_DGRAM)
		return;

	/* Prepare server on port #0 to be allowed. */
	ASSERT_LE(0, srv0_fd = socket_variant(&self->srv0));
	ASSERT_EQ(0, bind_variant(srv0_fd, &self->srv0));

	/* And another server on port #1 to be denied. */
	ASSERT_LE(0, srv1_fd = socket_variant(&self->srv1));
	ASSERT_EQ(0, bind_variant(srv1_fd, &self->srv1));

	/*
	 * Check that sockets connected before restrictions are not impacted in
	 * any way.
	 */
	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		ASSERT_LE(0, client_fd = socket_variant(&self->srv0));
		ASSERT_EQ(0, connect_variant(client_fd, &self->srv0));
		if (variant->sandbox == UDP_SANDBOX) {
			/* Deny all connect()/send(explicit addr)/bind(). */
			const struct landlock_ruleset_attr ruleset_attr = {
				.handled_access_net =
					LANDLOCK_ACCESS_NET_BIND_UDP |
					LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,
			};
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_attr, sizeof(ruleset_attr), 0);
			ASSERT_LE(0, ruleset_fd);
			enforce_ruleset(_metadata, ruleset_fd);
			EXPECT_EQ(0, close(ruleset_fd));
		}
		EXPECT_EQ(0,
			  test_sendmsg(_metadata, &variant->prot, client_fd,
				       srv0_fd, NULL, restricted, restricted));
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  srv0_fd, &self->srv0, restricted,
					  restricted));
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  srv1_fd, &self->srv1, restricted,
					  restricted));
		EXPECT_EQ(0, close(client_fd));
		_exit(_metadata->exit_code);
	}
	EXPECT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(1, WIFEXITED(status));
	EXPECT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

	/*
	 * Restrict connect/send, but not bind(). Then try sending with no
	 * destination (and no remote peer set), an allowed destination, then a
	 * denied destination.
	 */
	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		if (variant->sandbox == UDP_SANDBOX) {
			const struct landlock_ruleset_attr ruleset_attr = {
				.handled_access_net =
					LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,
			};
			const struct landlock_net_port_attr send_p0 = {
				.allowed_access =
					LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,
				.port = self->srv0.port,
			};
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_attr, sizeof(ruleset_attr), 0);
			ASSERT_LE(0, ruleset_fd);
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &send_p0, 0));
			enforce_ruleset(_metadata, ruleset_fd);
			EXPECT_EQ(0, close(ruleset_fd));
		}
		ASSERT_LE(0, client_fd = socket_variant(&self->srv0));
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  -1, NULL, false, false));
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  srv0_fd, &self->srv0, false, false));
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  srv1_fd, &self->srv1, false,
					  restricted));
		EXPECT_EQ(0, close(client_fd));
		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(1, WIFEXITED(status));
	EXPECT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

	/*
	 * Rest of this test is just for autobind enforcement, which only exists
	 * in IP sockets.
	 */
	if (variant->prot.domain != AF_INET && variant->prot.domain != AF_INET6)
		return;

	/* Restrict bind() to explicit calls with an arbitrary (non-0) port. */
	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		const uint16_t allowed_src_port = 42424;
		struct service_fixture allowed_src;

		allowed_src = self->srv0;
		set_port(&allowed_src, allowed_src_port);
		if (variant->sandbox == UDP_SANDBOX) {
			const struct landlock_ruleset_attr ruleset_attr = {
				.handled_access_net =
					LANDLOCK_ACCESS_NET_BIND_UDP,
			};
			const struct landlock_net_port_attr rule = {
				.allowed_access = LANDLOCK_ACCESS_NET_BIND_UDP,
				.port = allowed_src_port,
			};
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_attr, sizeof(ruleset_attr), 0);
			ASSERT_LE(0, ruleset_fd);
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &rule, 0));
			enforce_ruleset(_metadata, ruleset_fd);
			EXPECT_EQ(0, close(ruleset_fd));
		}
		ASSERT_LE(0, client_fd = socket_variant(&self->srv0));

		/* Check that implicit bind(0) in sendmsg() is denied. */
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  srv0_fd, &self->srv0, restricted,
					  false));

		/* Same thing for autobind in connect(). */
		res = connect_variant(client_fd, &self->srv0);
		if (restricted) {
			EXPECT_EQ(-EACCES, res);
		} else {
			EXPECT_EQ(0, res);
		}
		EXPECT_EQ(0, close(client_fd));

		/* Make sendmsg() work by explicitly binding to the only allowed port. */
		ASSERT_LE(0, client_fd = socket_variant(&self->srv0));
		EXPECT_EQ(0, bind_variant(client_fd, &allowed_src));
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  srv0_fd, &self->srv0, restricted,
					  false));
		EXPECT_EQ(0, close(client_fd));

		/* Make connect() work by explicitly binding to the only allowed port. */
		ASSERT_LE(0, client_fd = socket_variant(&self->srv0));
		EXPECT_EQ(0, bind_variant(client_fd, &allowed_src));
		EXPECT_EQ(0, connect_variant(client_fd, &self->srv0));
		EXPECT_EQ(0, close(client_fd));

		_exit(_metadata->exit_code);
		return;
	}
	EXPECT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(1, WIFEXITED(status));
	EXPECT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

	/*
	 * Check that %LANDLOCK_ACCESS_NET_BIND_UDP on port 0 allows implicit
	 * autobinds.
	 */
	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		if (variant->sandbox == UDP_SANDBOX) {
			const struct landlock_ruleset_attr ruleset_attr = {
				.handled_access_net =
					LANDLOCK_ACCESS_NET_BIND_UDP,
			};
			const struct landlock_net_port_attr rule = {
				.allowed_access = LANDLOCK_ACCESS_NET_BIND_UDP,
				.port = 0,
			};
			const int ruleset_fd = landlock_create_ruleset(
				&ruleset_attr, sizeof(ruleset_attr), 0);
			ASSERT_LE(0, ruleset_fd);
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &rule, 0));
			enforce_ruleset(_metadata, ruleset_fd);
			EXPECT_EQ(0, close(ruleset_fd));
		}
		ASSERT_LE(0, client_fd = socket_variant(&self->srv0));
		EXPECT_EQ(0, test_sendmsg(_metadata, &variant->prot, client_fd,
					  srv0_fd, &self->srv0, false, false));
		EXPECT_EQ(0, close(client_fd));
		_exit(_metadata->exit_code);
	}
	EXPECT_EQ(child, waitpid(child, &status, 0));
	EXPECT_EQ(1, WIFEXITED(status));
	EXPECT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}

TEST_F(protocol, sendmsg_unspec)
{
	const bool restricted = is_restricted(&variant->prot, variant->sandbox);
	int client_fd, srv0_fd, srv1_fd, res;
	char read_buf[1] = { 0 };

	/*
	 * We already test for the absence of influence on sendmsg for other
	 * socket types and other address families, there's no point in adapting
	 * this test for stream sockets too.
	 */
	if (variant->prot.type != SOCK_DGRAM)
		return;

	/* Prepare client of the right family. */
	ASSERT_LE(0, client_fd = socket_variant(&self->srv0));

	/* Prepare server on port #0 to be allowed. */
	ASSERT_LE(0, srv0_fd = socket_variant(&self->srv0));
	ASSERT_EQ(0, bind_variant(srv0_fd, &self->srv0));

	/* And another server on port #1 to be denied. */
	ASSERT_LE(0, srv1_fd = socket_variant(&self->srv1));
	ASSERT_EQ(0, bind_variant(srv1_fd, &self->srv1));

	if (variant->sandbox == UDP_SANDBOX) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net =
				LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,
		};
		const struct landlock_net_port_attr rule = {
			.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,
			.port = self->srv0.port,
		};
		const int ruleset_fd = landlock_create_ruleset(
			&ruleset_attr, sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &rule, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	/* Explicit AF_UNSPEC address but truncated. */
	EXPECT_EQ(-EINVAL, sendto_variant_addrlen(
				   client_fd, &self->unspec_srv0,
				   get_addrlen(&self->unspec_srv0, true) - 1,
				   "A", 1, 0));

	/*
	 * Explicit AF_UNSPEC address, should be treated as AF_INET by IPv4
	 * sockets (and thus map to srv0, allowed), but be denied by IPv6
	 * sockets.
	 */
	res = sendto_variant(client_fd, &self->unspec_srv0, "B", 1, 0);
	if (variant->prot.domain == AF_INET6) {
		if (restricted) {
			/* Always denied on IPv6 socket. */
			EXPECT_EQ(-EACCES, res);
		} else {
			/* IPv6 sockets treat AF_UNSPEC as a NULL address. */
			EXPECT_EQ(-EDESTADDRREQ, res);
		}
	} else if (variant->prot.domain == AF_INET) {
		ASSERT_EQ(0, res);
		EXPECT_EQ(1, read(srv0_fd, read_buf, 1))
		{
			TH_LOG("read() failed: %s", strerror(errno));
		}
		EXPECT_EQ(read_buf[0], 'B');
	} else {
		/* Unix sockets don't accept AF_UNSPEC. */
		EXPECT_EQ(-EINVAL, res);
	}

	/*
	 * Explicit AF_UNSPEC address, should be treated as AF_INET on IPv4
	 * sockets (and thus map to srv1, denied), and be denied on IPv6 sockets
	 * as always.
	 */
	res = sendto_variant(client_fd, &self->unspec_srv1, "C", 1, 0);
	if (variant->prot.domain == AF_INET6) {
		if (restricted) {
			/* Always denied on IPv6 socket. */
			EXPECT_EQ(-EACCES, res);
		} else {
			/* IPv6 sockets treat AF_UNSPEC as a NULL address. */
			EXPECT_EQ(-EDESTADDRREQ, res);
		}
	} else if (variant->prot.domain == AF_INET) {
		if (restricted) {
			/* Sending to srv1 is not allowed, only srv0. */
			EXPECT_EQ(-EACCES, res);
		} else {
			ASSERT_EQ(0, res);
			EXPECT_EQ(1, read(srv1_fd, read_buf, 1))
			{
				TH_LOG("read() failed: %s", strerror(errno));
			}
			EXPECT_EQ(read_buf[0], 'C');
		}
	} else {
		/* Unix sockets don't accept AF_UNSPEC. */
		EXPECT_EQ(-EINVAL, res);
	}

	ASSERT_EQ(0, connect_variant(client_fd, &self->srv0));

	/* Minimal explicit AF_UNSPEC address (just the sa_family_t field) */
	res = sendto_variant_addrlen(client_fd, &self->unspec_srv0,
				     get_addrlen(&self->unspec_srv0, true), "D",
				     1, 0);
	if (variant->prot.domain == AF_INET6) {
		if (restricted) {
			/* AF_UNSPEC is always denied in IPv6. */
			EXPECT_EQ(-EACCES, res);
		} else {
			/*
			 * IPv6 sockets treat AF_UNSPEC as a NULL address,
			 * falling back to the connected address.
			 */
			ASSERT_EQ(0, res);
			EXPECT_EQ(1, read(srv0_fd, read_buf, 1));
			EXPECT_EQ(read_buf[0], 'D');
		}
	} else {
		/*
		 * IPv4 socket will expect a struct sockaddr_in, our address is
		 * considered truncated.  And Unix sockets don't accept
		 * AF_UNSPEC at all.
		 */
		EXPECT_EQ(-EINVAL, res);
	}
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
FIXTURE_VARIANT_ADD(ipv4, udp_sandbox_with_tcp) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
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

/* clang-format off */
FIXTURE_VARIANT_ADD(ipv4, udp_sandbox_with_udp) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
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

	if (variant->sandbox == TCP_SANDBOX ||
	    variant->sandbox == UDP_SANDBOX) {
		const __u64 access_rights =
			(variant->sandbox == TCP_SANDBOX ?
				 LANDLOCK_ACCESS_NET_BIND_TCP |
					 LANDLOCK_ACCESS_NET_CONNECT_TCP :
				 LANDLOCK_ACCESS_NET_BIND_UDP |
					 LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = access_rights,
		};
		const struct landlock_net_port_attr tcp_bind_connect_p0 = {
			.allowed_access = access_rights,
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

#define ACCESS_LAST LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP

#define ACCESS_ALL ( \
	LANDLOCK_ACCESS_NET_BIND_TCP | \
	LANDLOCK_ACCESS_NET_CONNECT_TCP | \
	LANDLOCK_ACCESS_NET_BIND_UDP | \
	LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP)

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
			       (unsigned long long)access, strerror(errno));
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
	struct service_fixture cli1;
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
FIXTURE_VARIANT_ADD(port_specific, tcp_sandbox_with_ipv4) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(port_specific, udp_sandbox_with_ipv4) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET,
		.type = SOCK_DGRAM,
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
FIXTURE_VARIANT_ADD(port_specific, tcp_sandbox_with_ipv6) {
	/* clang-format on */
	.sandbox = TCP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(port_specific, udp_sandbox_with_ipv6) {
	/* clang-format on */
	.sandbox = UDP_SANDBOX,
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_DGRAM,
	},
};

FIXTURE_SETUP(port_specific)
{
	disable_caps(_metadata);

	ASSERT_EQ(0, set_service(&self->srv0, variant->prot, 0));
	ASSERT_EQ(0, set_service(&self->cli1, variant->prot, 1));

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
	if (variant->sandbox == TCP_SANDBOX ||
	    variant->sandbox == UDP_SANDBOX) {
		const __u64 access_rights =
			(variant->sandbox == TCP_SANDBOX ?
				 LANDLOCK_ACCESS_NET_BIND_TCP |
					 LANDLOCK_ACCESS_NET_CONNECT_TCP :
				 LANDLOCK_ACCESS_NET_BIND_UDP |
					 LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = access_rights,
		};
		const struct landlock_net_port_attr bind_connect_zero = {
			.allowed_access = access_rights,
			.port = 0,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		/* Checks zero port value on bind and connect actions. */
		EXPECT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &bind_connect_zero, 0));

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

	if (variant->prot.type == SOCK_STREAM)
		EXPECT_EQ(0, listen(bind_fd, backlog));

	/* Connects on port 0. */
	ret = connect_variant(connect_fd, &self->srv0);
	if (variant->prot.type == SOCK_STREAM) {
		EXPECT_EQ(-ECONNREFUSED, ret);
	} else {
		EXPECT_EQ(0, ret);
	}

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
	if (variant->sandbox == TCP_SANDBOX ||
	    variant->sandbox == UDP_SANDBOX) {
		const __u64 bind_right = (variant->sandbox == TCP_SANDBOX ?
						  LANDLOCK_ACCESS_NET_BIND_TCP :
						  LANDLOCK_ACCESS_NET_BIND_UDP);
		const __u64 access_rights =
			(variant->sandbox == TCP_SANDBOX ?
				 (LANDLOCK_ACCESS_NET_BIND_TCP |
				  LANDLOCK_ACCESS_NET_CONNECT_TCP) :
				 (LANDLOCK_ACCESS_NET_BIND_UDP |
				  LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP));
		const struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_net = access_rights,
		};
		/* A rule with port value less than 1024. */
		const struct landlock_net_port_attr bind_connect_low_range = {
			.allowed_access = access_rights,
			.port = 1023,
		};
		/* A rule with 1024 port. */
		const struct landlock_net_port_attr bind_connect = {
			.allowed_access = access_rights,
			.port = 1024,
		};
		/* A rule with cli1's port, to use as source port. */
		const struct landlock_net_port_attr srcport = {
			.allowed_access = bind_right,
			.port = self->cli1.port,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);

		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &bind_connect_low_range, 0));
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
					    &bind_connect, 0));
		if (variant->sandbox == UDP_SANDBOX) {
			ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
						       LANDLOCK_RULE_NET_PORT,
						       &srcport, 0));
		}

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	bind_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, bind_fd);

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
	if (variant->prot.type == SOCK_STREAM)
		EXPECT_EQ(0, listen(bind_fd, backlog));

	connect_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, connect_fd);
	if (variant->prot.type == SOCK_DGRAM) {
		/*
		 * We are about to connect(), but bind() is restricted, so for
		 * UDP sockets we need to use cli1's port as source port (the
		 * only one we are allowed to use).
		 */
		EXPECT_EQ(0, bind_variant(connect_fd, &self->cli1));
	}
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
	if (variant->prot.type == SOCK_STREAM)
		EXPECT_EQ(0, listen(bind_fd, backlog));
	if (variant->prot.type == SOCK_DGRAM)
		EXPECT_EQ(0, bind_variant(connect_fd, &self->cli1));

	/* Connects on the binded port 1024. */
	ret = connect_variant(connect_fd, &self->srv0);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(0, close(connect_fd));
	EXPECT_EQ(0, close(bind_fd));
}

/**
 * matches_auditlog - Check audit log for a network access denial
 *
 * @audit_fd:   Audit file descriptor.
 * @blockers:   A regex-escaped blocker string, e.g., "net\.bind_tcp".
 * @dir_addr:   Either "saddr" or "daddr", ignored if addr is NULL.
 * @addr:       A regex-escaped IP address string, or NULL.
 * @dir_port:   Either "src" or "dest", ignored if addr is NULL.
 * @port:       A port number, ignored if addr is NULL.
 */
static int matches_auditlog(const int audit_fd, const char *const blockers,
			    const char *const dir_addr, const char *const addr,
			    const char *const dir_port, const __u16 port)
{
	static const char log_with_addrport_tmpl[] = REGEX_LANDLOCK_PREFIX
		" blockers=%s %s=%s %s=%u$";
	static const char log_without_addrport_tmpl[] = REGEX_LANDLOCK_PREFIX
		" blockers=%s";
	/*
	 * Max strlen(blockers): 16
	 * Max strlen(dir_addr): 5
	 * Max strlen(addr): 12
	 * Max strlen(dir_port): 4
	 * Max strlen(%u port): 5
	 */
	char log_match[sizeof(log_with_addrport_tmpl) + 42];
	int log_match_len;

	if (addr == NULL)
		log_match_len = snprintf(log_match, sizeof(log_match),
					 log_without_addrport_tmpl, blockers);
	else
		log_match_len = snprintf(log_match, sizeof(log_match),
					 log_with_addrport_tmpl, blockers,
					 dir_addr, addr, dir_port, port);
	if (log_match_len > sizeof(log_match))
		return -E2BIG;

	return audit_match_record(audit_fd, AUDIT_LANDLOCK_ACCESS, log_match,
				  NULL);
}

FIXTURE(audit)
{
	struct service_fixture srv0;
	struct service_fixture srv1;
	/* srv2 has a rule with no access but quiet bit set. */
	struct service_fixture srv2;
	struct service_fixture unspec_srv0;
	struct audit_filter audit_filter;
	int audit_fd;
};

FIXTURE_VARIANT(audit)
{
	const char *const addr;
	const struct protocol_variant prot;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit, ipv4_tcp) {
	/* clang-format on */
	.addr = "127\\.0\\.0\\.1",
	.prot = {
		.domain = AF_INET,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit, ipv4_udp) {
	/* clang-format on */
	.addr = "127\\.0\\.0\\.1",
	.prot = {
		.domain = AF_INET,
		.type = SOCK_DGRAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit, ipv6_tcp) {
	/* clang-format on */
	.addr = "::1",
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_STREAM,
	},
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit, ipv6_udp) {
	/* clang-format on */
	.addr = "::1",
	.prot = {
		.domain = AF_INET6,
		.type = SOCK_DGRAM,
	},
};

FIXTURE_SETUP(audit)
{
	struct protocol_variant prot_unspec = variant->prot;

	prot_unspec.domain = AF_UNSPEC;

	ASSERT_EQ(0, set_service(&self->srv0, variant->prot, 0));
	ASSERT_EQ(0, set_service(&self->srv1, variant->prot, 1));
	ASSERT_EQ(0, set_service(&self->srv2, variant->prot, 2));
	ASSERT_EQ(0, set_service(&self->unspec_srv0, prot_unspec, 0));

	setup_loopback(_metadata);

	set_cap(_metadata, CAP_AUDIT_CONTROL);
	self->audit_fd = audit_init_with_exe_filter(&self->audit_filter);
	EXPECT_LE(0, self->audit_fd);
	disable_caps(_metadata);
};

FIXTURE_TEARDOWN(audit)
{
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, audit_cleanup(self->audit_fd, &self->audit_filter));
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

TEST_F(audit, bind)
{
	const char *audit_evt = (variant->prot.type == SOCK_STREAM ?
					 "net\\.bind_tcp" :
					 "net\\.bind_udp");
	const __u64 access_rights =
		(variant->prot.type == SOCK_STREAM ?
			 LANDLOCK_ACCESS_NET_BIND_TCP |
				 LANDLOCK_ACCESS_NET_CONNECT_TCP :
			 LANDLOCK_ACCESS_NET_BIND_UDP |
				 LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = access_rights,
		.quiet_access_net = access_rights,
	};
	const struct landlock_net_port_attr quiet_rule = {
		.allowed_access = 0,
		.port = self->srv2.port,
	};
	struct audit_records records;
	int ruleset_fd, sock_fd;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &quiet_rule, LANDLOCK_ADD_RULE_QUIET));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	sock_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(-EACCES, bind_variant(sock_fd, &self->srv0));
	EXPECT_EQ(0, matches_auditlog(self->audit_fd, audit_evt, "saddr",
				      variant->addr, "src", self->srv0.port));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(1, records.domain);

	EXPECT_EQ(0, close(sock_fd));

	/* Bind to srv2 (with quiet rule): no new audit logs. */
	sock_fd = socket_variant(&self->srv2);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(-EACCES, bind_variant(sock_fd, &self->srv2));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);

	EXPECT_EQ(0, close(sock_fd));
}

TEST_F(audit, connect)
{
	const char *audit_evt = (variant->prot.type == SOCK_STREAM ?
					 "net\\.connect_tcp" :
					 "net\\.connect_send_udp");
	const __u64 bind_right = (variant->prot.type == SOCK_STREAM ?
					  LANDLOCK_ACCESS_NET_BIND_TCP :
					  LANDLOCK_ACCESS_NET_BIND_UDP);
	const __u64 conn_right = (variant->prot.type == SOCK_STREAM ?
					  LANDLOCK_ACCESS_NET_CONNECT_TCP :
					  LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
	const __u64 access_rights = bind_right | conn_right;
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = access_rights,
		.quiet_access_net = access_rights,
	};
	const struct landlock_net_port_attr rule_connect_p1 = {
		.allowed_access = conn_right,
		.port = self->srv1.port,
	};
	const struct landlock_net_port_attr quiet_rule = {
		.allowed_access = 0,
		.port = self->srv2.port,
	};
	struct audit_records records;
	int ruleset_fd, sock_fd;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &rule_connect_p1, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &quiet_rule, LANDLOCK_ADD_RULE_QUIET));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	sock_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(-EACCES, connect_variant(sock_fd, &self->srv0));
	EXPECT_EQ(0, matches_auditlog(self->audit_fd, audit_evt, "daddr",
				      variant->addr, "dest", self->srv0.port));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(1, records.domain);

	if (variant->prot.type == SOCK_DGRAM) {
		/* Check that autobind generates a denied bind event. */
		EXPECT_EQ(-EACCES, connect_variant(sock_fd, &self->srv1));

		EXPECT_EQ(0, matches_auditlog(self->audit_fd, "net\\.bind_udp",
					      NULL, NULL, NULL, 0));
		EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
		EXPECT_EQ(0, records.access);
		EXPECT_EQ(0, records.domain);
	}

	EXPECT_EQ(0, close(sock_fd));

	/* Connect to srv2 (with quiet rule): no new audit logs. */
	sock_fd = socket_variant(&self->srv2);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(-EACCES, connect_variant(sock_fd, &self->srv2));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);

	EXPECT_EQ(0, close(sock_fd));
}

/* Quieting bind access has no effect on connect. */
TEST_F(audit, connect_quiet_bind)
{
	const char *audit_evt = (variant->prot.type == SOCK_STREAM ?
					 "net\\.connect_tcp" :
					 "net\\.connect_send_udp");
	const int bind_right = (variant->prot.type == SOCK_STREAM ?
					LANDLOCK_ACCESS_NET_BIND_TCP :
					LANDLOCK_ACCESS_NET_BIND_UDP);
	const int conn_right = (variant->prot.type == SOCK_STREAM ?
					LANDLOCK_ACCESS_NET_CONNECT_TCP :
					LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
	const int access_rights = bind_right | conn_right;
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = access_rights,
		.quiet_access_net = bind_right,
	};
	const struct landlock_ruleset_attr ruleset_attr_2 = {
		.handled_access_net = access_rights,
		.quiet_access_net = conn_right,
	};
	const struct landlock_net_port_attr quiet_rule = {
		.allowed_access = 0,
		.port = self->srv2.port,
	};
	struct audit_records records;
	int ruleset_fd, sock_fd;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &quiet_rule, LANDLOCK_ADD_RULE_QUIET));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	sock_fd = socket_variant(&self->srv2);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(-EACCES, connect_variant(sock_fd, &self->srv2));
	EXPECT_EQ(0, matches_auditlog(self->audit_fd, audit_evt, "daddr",
				      variant->addr, "dest", self->srv2.port));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	EXPECT_EQ(0, close(sock_fd));

	/* New layer that also denies connect but has the correct quiet bit. */
	ruleset_fd = landlock_create_ruleset(&ruleset_attr_2,
					     sizeof(ruleset_attr_2), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &quiet_rule, LANDLOCK_ADD_RULE_QUIET));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	sock_fd = socket_variant(&self->srv2);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(-EACCES, connect_variant(sock_fd, &self->srv2));

	/* Quieted - no logs expected. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	EXPECT_EQ(0, close(sock_fd));
}

static int matches_log_connect_bound(int audit_fd, const char *const blockers,
				     const char *const addr, __u16 lport,
				     __u16 dport)
{
	static const char log_template[] = REGEX_LANDLOCK_PREFIX
		" blockers=%s laddr=%s lport=%u daddr=%s dest=%u$";
	/* Slack for the blockers, two addresses and two port numbers. */
	char log_match[sizeof(log_template) + 60];
	int log_match_len;

	log_match_len = snprintf(log_match, sizeof(log_match), log_template,
				 blockers, addr, lport, addr, dport);
	if (log_match_len > sizeof(log_match))
		return -E2BIG;

	return audit_match_record(audit_fd, AUDIT_LANDLOCK_ACCESS, log_match,
				  NULL);
}

/*
 * After a bind() to an allowed port, a denied connect must report laddr/lport
 * from the bound socket (made available through audit_net.sk) in addition to
 * the connect sockaddr's daddr/dest.
 */
TEST_F(audit, connect_bound)
{
	const __u64 bind_right = (variant->prot.type == SOCK_STREAM ?
					  LANDLOCK_ACCESS_NET_BIND_TCP :
					  LANDLOCK_ACCESS_NET_BIND_UDP);
	const __u64 conn_right = (variant->prot.type == SOCK_STREAM ?
					  LANDLOCK_ACCESS_NET_CONNECT_TCP :
					  LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP);
	const char *const audit_evt = (variant->prot.type == SOCK_STREAM ?
					       "net\\.connect_tcp" :
					       "net\\.connect_send_udp");
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = bind_right | conn_right,
	};
	const struct landlock_net_port_attr rule_bind = {
		.allowed_access = bind_right,
		.port = self->srv0.port,
	};
	struct service_fixture srv_remote;
	struct audit_records records;
	int ruleset_fd, sock_fd;

	/* Uses a second port as the denied connect target. */
	ASSERT_EQ(0, set_service(&srv_remote, variant->prot, 1));

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &rule_bind, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	sock_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(0, bind_variant(sock_fd, &self->srv0));
	EXPECT_EQ(-EACCES, connect_variant(sock_fd, &srv_remote));
	EXPECT_EQ(0, matches_log_connect_bound(self->audit_fd, audit_evt,
					       variant->addr, self->srv0.port,
					       srv_remote.port));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(1, records.domain);

	EXPECT_EQ(0, close(sock_fd));
}

TEST_F(audit, sendmsg)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP |
				      LANDLOCK_ACCESS_NET_BIND_UDP,
	};
	const struct landlock_net_port_attr rule = {
		.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP,
		.port = self->srv1.port,
	};
	struct audit_records records;
	int ruleset_fd;
	int sock_fd;

	/* Sendmsg on stream sockets is never denied. */
	if (variant->prot.type != SOCK_DGRAM)
		return;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &rule, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	sock_fd = socket_variant(&self->srv0);
	ASSERT_LE(0, sock_fd);
	EXPECT_EQ(-EACCES, sendto_variant(sock_fd, &self->srv0, "A", 1, 0));
	EXPECT_EQ(0, matches_auditlog(self->audit_fd, "net\\.connect_send_udp",
				      "daddr", variant->addr, "dest",
				      self->srv0.port));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(1, records.domain);

	/* Check that autobind generates a denied bind event. */
	EXPECT_EQ(-EACCES, sendto_variant(sock_fd, &self->srv1, "A", 1, 0));
	EXPECT_EQ(0, matches_auditlog(self->audit_fd, "net\\.bind_udp", NULL,
				      NULL, NULL, 0));
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);

	EXPECT_EQ(-EACCES,
		  sendto_variant(sock_fd, &self->unspec_srv0, "B", 1, 0));
	EXPECT_EQ(0, matches_auditlog(self->audit_fd, "net\\.connect_send_udp",
				      "daddr", NULL, "dest", 0));
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);

	EXPECT_EQ(0, close(sock_fd));
}

TEST_HARNESS_MAIN
