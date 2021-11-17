// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2020 Cloudflare
/*
 * Test BPF attach point for INET socket lookup (BPF_SK_LOOKUP).
 *
 * Tests exercise:
 *  - attaching/detaching/querying programs to BPF_SK_LOOKUP hook,
 *  - redirecting socket lookup to a socket selected by BPF program,
 *  - failing a socket lookup on BPF program's request,
 *  - error scenarios for selecting a socket from BPF program,
 *  - accessing BPF program context,
 *  - attaching and running multiple BPF programs.
 *
 * Tests run in a dedicated network namespace.
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "test_progs.h"
#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "testing_helpers.h"
#include "test_sk_lookup.skel.h"

/* External (address, port) pairs the client sends packets to. */
#define EXT_IP4		"127.0.0.1"
#define EXT_IP6		"fd00::1"
#define EXT_PORT	7007

/* Internal (address, port) pairs the server listens/receives at. */
#define INT_IP4		"127.0.0.2"
#define INT_IP4_V6	"::ffff:127.0.0.2"
#define INT_IP6		"fd00::2"
#define INT_PORT	8008

#define IO_TIMEOUT_SEC	3

enum server {
	SERVER_A = 0,
	SERVER_B = 1,
	MAX_SERVERS,
};

enum {
	PROG1 = 0,
	PROG2,
};

struct inet_addr {
	const char *ip;
	unsigned short port;
};

struct test {
	const char *desc;
	struct bpf_program *lookup_prog;
	struct bpf_program *reuseport_prog;
	struct bpf_map *sock_map;
	int sotype;
	struct inet_addr connect_to;
	struct inet_addr listen_at;
	enum server accept_on;
	bool reuseport_has_conns; /* Add a connected socket to reuseport group */
};

static __u32 duration;		/* for CHECK macro */

static bool is_ipv6(const char *ip)
{
	return !!strchr(ip, ':');
}

static int attach_reuseport(int sock_fd, struct bpf_program *reuseport_prog)
{
	int err, prog_fd;

	prog_fd = bpf_program__fd(reuseport_prog);
	if (prog_fd < 0) {
		errno = -prog_fd;
		return -1;
	}

	err = setsockopt(sock_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF,
			 &prog_fd, sizeof(prog_fd));
	if (err)
		return -1;

	return 0;
}

static socklen_t inetaddr_len(const struct sockaddr_storage *addr)
{
	return (addr->ss_family == AF_INET ? sizeof(struct sockaddr_in) :
		addr->ss_family == AF_INET6 ? sizeof(struct sockaddr_in6) : 0);
}

static int make_socket(int sotype, const char *ip, int port,
		       struct sockaddr_storage *addr)
{
	struct timeval timeo = { .tv_sec = IO_TIMEOUT_SEC };
	int err, family, fd;

	family = is_ipv6(ip) ? AF_INET6 : AF_INET;
	err = make_sockaddr(family, ip, port, addr, NULL);
	if (CHECK(err, "make_address", "failed\n"))
		return -1;

	fd = socket(addr->ss_family, sotype, 0);
	if (CHECK(fd < 0, "socket", "failed\n")) {
		log_err("failed to make socket");
		return -1;
	}

	err = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeo, sizeof(timeo));
	if (CHECK(err, "setsockopt(SO_SNDTIMEO)", "failed\n")) {
		log_err("failed to set SNDTIMEO");
		close(fd);
		return -1;
	}

	err = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeo, sizeof(timeo));
	if (CHECK(err, "setsockopt(SO_RCVTIMEO)", "failed\n")) {
		log_err("failed to set RCVTIMEO");
		close(fd);
		return -1;
	}

	return fd;
}

static int make_server(int sotype, const char *ip, int port,
		       struct bpf_program *reuseport_prog)
{
	struct sockaddr_storage addr = {0};
	const int one = 1;
	int err, fd = -1;

	fd = make_socket(sotype, ip, port, &addr);
	if (fd < 0)
		return -1;

	/* Enabled for UDPv6 sockets for IPv4-mapped IPv6 to work. */
	if (sotype == SOCK_DGRAM) {
		err = setsockopt(fd, SOL_IP, IP_RECVORIGDSTADDR, &one,
				 sizeof(one));
		if (CHECK(err, "setsockopt(IP_RECVORIGDSTADDR)", "failed\n")) {
			log_err("failed to enable IP_RECVORIGDSTADDR");
			goto fail;
		}
	}

	if (sotype == SOCK_DGRAM && addr.ss_family == AF_INET6) {
		err = setsockopt(fd, SOL_IPV6, IPV6_RECVORIGDSTADDR, &one,
				 sizeof(one));
		if (CHECK(err, "setsockopt(IPV6_RECVORIGDSTADDR)", "failed\n")) {
			log_err("failed to enable IPV6_RECVORIGDSTADDR");
			goto fail;
		}
	}

	if (sotype == SOCK_STREAM) {
		err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one,
				 sizeof(one));
		if (CHECK(err, "setsockopt(SO_REUSEADDR)", "failed\n")) {
			log_err("failed to enable SO_REUSEADDR");
			goto fail;
		}
	}

	if (reuseport_prog) {
		err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one,
				 sizeof(one));
		if (CHECK(err, "setsockopt(SO_REUSEPORT)", "failed\n")) {
			log_err("failed to enable SO_REUSEPORT");
			goto fail;
		}
	}

	err = bind(fd, (void *)&addr, inetaddr_len(&addr));
	if (CHECK(err, "bind", "failed\n")) {
		log_err("failed to bind listen socket");
		goto fail;
	}

	if (sotype == SOCK_STREAM) {
		err = listen(fd, SOMAXCONN);
		if (CHECK(err, "make_server", "listen")) {
			log_err("failed to listen on port %d", port);
			goto fail;
		}
	}

	/* Late attach reuseport prog so we can have one init path */
	if (reuseport_prog) {
		err = attach_reuseport(fd, reuseport_prog);
		if (CHECK(err, "attach_reuseport", "failed\n")) {
			log_err("failed to attach reuseport prog");
			goto fail;
		}
	}

	return fd;
fail:
	close(fd);
	return -1;
}

static int make_client(int sotype, const char *ip, int port)
{
	struct sockaddr_storage addr = {0};
	int err, fd;

	fd = make_socket(sotype, ip, port, &addr);
	if (fd < 0)
		return -1;

	err = connect(fd, (void *)&addr, inetaddr_len(&addr));
	if (CHECK(err, "make_client", "connect")) {
		log_err("failed to connect client socket");
		goto fail;
	}

	return fd;
fail:
	close(fd);
	return -1;
}

static __u64 socket_cookie(int fd)
{
	__u64 cookie;
	socklen_t cookie_len = sizeof(cookie);

	if (CHECK(getsockopt(fd, SOL_SOCKET, SO_COOKIE, &cookie, &cookie_len) < 0,
		  "getsockopt(SO_COOKIE)", "%s\n", strerror(errno)))
		return 0;
	return cookie;
}

static int fill_sk_lookup_ctx(struct bpf_sk_lookup *ctx, const char *local_ip, __u16 local_port,
			      const char *remote_ip, __u16 remote_port)
{
	void *local, *remote;
	int err;

	memset(ctx, 0, sizeof(*ctx));
	ctx->local_port = local_port;
	ctx->remote_port = htons(remote_port);

	if (is_ipv6(local_ip)) {
		ctx->family = AF_INET6;
		local = &ctx->local_ip6[0];
		remote = &ctx->remote_ip6[0];
	} else {
		ctx->family = AF_INET;
		local = &ctx->local_ip4;
		remote = &ctx->remote_ip4;
	}

	err = inet_pton(ctx->family, local_ip, local);
	if (CHECK(err != 1, "inet_pton", "local_ip failed\n"))
		return 1;

	err = inet_pton(ctx->family, remote_ip, remote);
	if (CHECK(err != 1, "inet_pton", "remote_ip failed\n"))
		return 1;

	return 0;
}

static int send_byte(int fd)
{
	ssize_t n;

	errno = 0;
	n = send(fd, "a", 1, 0);
	if (CHECK(n <= 0, "send_byte", "send")) {
		log_err("failed/partial send");
		return -1;
	}
	return 0;
}

static int recv_byte(int fd)
{
	char buf[1];
	ssize_t n;

	n = recv(fd, buf, sizeof(buf), 0);
	if (CHECK(n <= 0, "recv_byte", "recv")) {
		log_err("failed/partial recv");
		return -1;
	}
	return 0;
}

static int tcp_recv_send(int server_fd)
{
	char buf[1];
	int ret, fd;
	ssize_t n;

	fd = accept(server_fd, NULL, NULL);
	if (CHECK(fd < 0, "accept", "failed\n")) {
		log_err("failed to accept");
		return -1;
	}

	n = recv(fd, buf, sizeof(buf), 0);
	if (CHECK(n <= 0, "recv", "failed\n")) {
		log_err("failed/partial recv");
		ret = -1;
		goto close;
	}

	n = send(fd, buf, n, 0);
	if (CHECK(n <= 0, "send", "failed\n")) {
		log_err("failed/partial send");
		ret = -1;
		goto close;
	}

	ret = 0;
close:
	close(fd);
	return ret;
}

static void v4_to_v6(struct sockaddr_storage *ss)
{
	struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)ss;
	struct sockaddr_in v4 = *(struct sockaddr_in *)ss;

	v6->sin6_family = AF_INET6;
	v6->sin6_port = v4.sin_port;
	v6->sin6_addr.s6_addr[10] = 0xff;
	v6->sin6_addr.s6_addr[11] = 0xff;
	memcpy(&v6->sin6_addr.s6_addr[12], &v4.sin_addr.s_addr, 4);
	memset(&v6->sin6_addr.s6_addr[0], 0, 10);
}

static int udp_recv_send(int server_fd)
{
	char cmsg_buf[CMSG_SPACE(sizeof(struct sockaddr_storage))];
	struct sockaddr_storage _src_addr = { 0 };
	struct sockaddr_storage *src_addr = &_src_addr;
	struct sockaddr_storage *dst_addr = NULL;
	struct msghdr msg = { 0 };
	struct iovec iov = { 0 };
	struct cmsghdr *cm;
	char buf[1];
	int ret, fd;
	ssize_t n;

	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);

	msg.msg_name = src_addr;
	msg.msg_namelen = sizeof(*src_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);

	errno = 0;
	n = recvmsg(server_fd, &msg, 0);
	if (CHECK(n <= 0, "recvmsg", "failed\n")) {
		log_err("failed to receive");
		return -1;
	}
	if (CHECK(msg.msg_flags & MSG_CTRUNC, "recvmsg", "truncated cmsg\n"))
		return -1;

	for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
		if ((cm->cmsg_level == SOL_IP &&
		     cm->cmsg_type == IP_ORIGDSTADDR) ||
		    (cm->cmsg_level == SOL_IPV6 &&
		     cm->cmsg_type == IPV6_ORIGDSTADDR)) {
			dst_addr = (struct sockaddr_storage *)CMSG_DATA(cm);
			break;
		}
		log_err("warning: ignored cmsg at level %d type %d",
			cm->cmsg_level, cm->cmsg_type);
	}
	if (CHECK(!dst_addr, "recvmsg", "missing ORIGDSTADDR\n"))
		return -1;

	/* Server socket bound to IPv4-mapped IPv6 address */
	if (src_addr->ss_family == AF_INET6 &&
	    dst_addr->ss_family == AF_INET) {
		v4_to_v6(dst_addr);
	}

	/* Reply from original destination address. */
	fd = socket(dst_addr->ss_family, SOCK_DGRAM, 0);
	if (CHECK(fd < 0, "socket", "failed\n")) {
		log_err("failed to create tx socket");
		return -1;
	}

	ret = bind(fd, (struct sockaddr *)dst_addr, sizeof(*dst_addr));
	if (CHECK(ret, "bind", "failed\n")) {
		log_err("failed to bind tx socket");
		goto out;
	}

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	n = sendmsg(fd, &msg, 0);
	if (CHECK(n <= 0, "sendmsg", "failed\n")) {
		log_err("failed to send echo reply");
		ret = -1;
		goto out;
	}

	ret = 0;
out:
	close(fd);
	return ret;
}

static int tcp_echo_test(int client_fd, int server_fd)
{
	int err;

	err = send_byte(client_fd);
	if (err)
		return -1;
	err = tcp_recv_send(server_fd);
	if (err)
		return -1;
	err = recv_byte(client_fd);
	if (err)
		return -1;

	return 0;
}

static int udp_echo_test(int client_fd, int server_fd)
{
	int err;

	err = send_byte(client_fd);
	if (err)
		return -1;
	err = udp_recv_send(server_fd);
	if (err)
		return -1;
	err = recv_byte(client_fd);
	if (err)
		return -1;

	return 0;
}

static struct bpf_link *attach_lookup_prog(struct bpf_program *prog)
{
	struct bpf_link *link;
	int net_fd;

	net_fd = open("/proc/self/ns/net", O_RDONLY);
	if (CHECK(net_fd < 0, "open", "failed\n")) {
		log_err("failed to open /proc/self/ns/net");
		return NULL;
	}

	link = bpf_program__attach_netns(prog, net_fd);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_netns")) {
		errno = -PTR_ERR(link);
		log_err("failed to attach program '%s' to netns",
			bpf_program__name(prog));
		link = NULL;
	}

	close(net_fd);
	return link;
}

static int update_lookup_map(struct bpf_map *map, int index, int sock_fd)
{
	int err, map_fd;
	uint64_t value;

	map_fd = bpf_map__fd(map);
	if (CHECK(map_fd < 0, "bpf_map__fd", "failed\n")) {
		errno = -map_fd;
		log_err("failed to get map FD");
		return -1;
	}

	value = (uint64_t)sock_fd;
	err = bpf_map_update_elem(map_fd, &index, &value, BPF_NOEXIST);
	if (CHECK(err, "bpf_map_update_elem", "failed\n")) {
		log_err("failed to update redir_map @ %d", index);
		return -1;
	}

	return 0;
}

static void query_lookup_prog(struct test_sk_lookup *skel)
{
	struct bpf_link *link[3] = {};
	struct bpf_link_info info;
	__u32 attach_flags = 0;
	__u32 prog_ids[3] = {};
	__u32 prog_cnt = 3;
	__u32 prog_id;
	int net_fd;
	int err;

	net_fd = open("/proc/self/ns/net", O_RDONLY);
	if (CHECK(net_fd < 0, "open", "failed\n")) {
		log_err("failed to open /proc/self/ns/net");
		return;
	}

	link[0] = attach_lookup_prog(skel->progs.lookup_pass);
	if (!link[0])
		goto close;
	link[1] = attach_lookup_prog(skel->progs.lookup_pass);
	if (!link[1])
		goto detach;
	link[2] = attach_lookup_prog(skel->progs.lookup_drop);
	if (!link[2])
		goto detach;

	err = bpf_prog_query(net_fd, BPF_SK_LOOKUP, 0 /* query flags */,
			     &attach_flags, prog_ids, &prog_cnt);
	if (CHECK(err, "bpf_prog_query", "failed\n")) {
		log_err("failed to query lookup prog");
		goto detach;
	}

	errno = 0;
	if (CHECK(attach_flags != 0, "bpf_prog_query",
		  "wrong attach_flags on query: %u", attach_flags))
		goto detach;
	if (CHECK(prog_cnt != 3, "bpf_prog_query",
		  "wrong program count on query: %u", prog_cnt))
		goto detach;
	prog_id = link_info_prog_id(link[0], &info);
	CHECK(prog_ids[0] != prog_id, "bpf_prog_query",
	      "invalid program #0 id on query: %u != %u\n",
	      prog_ids[0], prog_id);
	CHECK(info.netns.netns_ino == 0, "netns_ino",
	      "unexpected netns_ino: %u\n", info.netns.netns_ino);
	prog_id = link_info_prog_id(link[1], &info);
	CHECK(prog_ids[1] != prog_id, "bpf_prog_query",
	      "invalid program #1 id on query: %u != %u\n",
	      prog_ids[1], prog_id);
	CHECK(info.netns.netns_ino == 0, "netns_ino",
	      "unexpected netns_ino: %u\n", info.netns.netns_ino);
	prog_id = link_info_prog_id(link[2], &info);
	CHECK(prog_ids[2] != prog_id, "bpf_prog_query",
	      "invalid program #2 id on query: %u != %u\n",
	      prog_ids[2], prog_id);
	CHECK(info.netns.netns_ino == 0, "netns_ino",
	      "unexpected netns_ino: %u\n", info.netns.netns_ino);

	err = bpf_link__detach(link[0]);
	if (CHECK(err, "link_detach", "failed %d\n", err))
		goto detach;

	/* prog id is still there, but netns_ino is zeroed out */
	prog_id = link_info_prog_id(link[0], &info);
	CHECK(prog_ids[0] != prog_id, "bpf_prog_query",
	      "invalid program #0 id on query: %u != %u\n",
	      prog_ids[0], prog_id);
	CHECK(info.netns.netns_ino != 0, "netns_ino",
	      "unexpected netns_ino: %u\n", info.netns.netns_ino);

detach:
	if (link[2])
		bpf_link__destroy(link[2]);
	if (link[1])
		bpf_link__destroy(link[1]);
	if (link[0])
		bpf_link__destroy(link[0]);
close:
	close(net_fd);
}

static void run_lookup_prog(const struct test *t)
{
	int server_fds[MAX_SERVERS] = { -1 };
	int client_fd, reuse_conn_fd = -1;
	struct bpf_link *lookup_link;
	int i, err;

	lookup_link = attach_lookup_prog(t->lookup_prog);
	if (!lookup_link)
		return;

	for (i = 0; i < ARRAY_SIZE(server_fds); i++) {
		server_fds[i] = make_server(t->sotype, t->listen_at.ip,
					    t->listen_at.port,
					    t->reuseport_prog);
		if (server_fds[i] < 0)
			goto close;

		err = update_lookup_map(t->sock_map, i, server_fds[i]);
		if (err)
			goto close;

		/* want just one server for non-reuseport test */
		if (!t->reuseport_prog)
			break;
	}

	/* Regular UDP socket lookup with reuseport behaves
	 * differently when reuseport group contains connected
	 * sockets. Check that adding a connected UDP socket to the
	 * reuseport group does not affect how reuseport works with
	 * BPF socket lookup.
	 */
	if (t->reuseport_has_conns) {
		struct sockaddr_storage addr = {};
		socklen_t len = sizeof(addr);

		/* Add an extra socket to reuseport group */
		reuse_conn_fd = make_server(t->sotype, t->listen_at.ip,
					    t->listen_at.port,
					    t->reuseport_prog);
		if (reuse_conn_fd < 0)
			goto close;

		/* Connect the extra socket to itself */
		err = getsockname(reuse_conn_fd, (void *)&addr, &len);
		if (CHECK(err, "getsockname", "errno %d\n", errno))
			goto close;
		err = connect(reuse_conn_fd, (void *)&addr, len);
		if (CHECK(err, "connect", "errno %d\n", errno))
			goto close;
	}

	client_fd = make_client(t->sotype, t->connect_to.ip, t->connect_to.port);
	if (client_fd < 0)
		goto close;

	if (t->sotype == SOCK_STREAM)
		tcp_echo_test(client_fd, server_fds[t->accept_on]);
	else
		udp_echo_test(client_fd, server_fds[t->accept_on]);

	close(client_fd);
close:
	if (reuse_conn_fd != -1)
		close(reuse_conn_fd);
	for (i = 0; i < ARRAY_SIZE(server_fds); i++) {
		if (server_fds[i] != -1)
			close(server_fds[i]);
	}
	bpf_link__destroy(lookup_link);
}

static void test_redirect_lookup(struct test_sk_lookup *skel)
{
	const struct test tests[] = {
		{
			.desc		= "TCP IPv4 redir port",
			.lookup_prog	= skel->progs.redir_port,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { EXT_IP4, INT_PORT },
		},
		{
			.desc		= "TCP IPv4 redir addr",
			.lookup_prog	= skel->progs.redir_ip4,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, EXT_PORT },
		},
		{
			.desc		= "TCP IPv4 redir with reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, INT_PORT },
			.accept_on	= SERVER_B,
		},
		{
			.desc		= "TCP IPv4 redir skip reuseport",
			.lookup_prog	= skel->progs.select_sock_a_no_reuseport,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, INT_PORT },
			.accept_on	= SERVER_A,
		},
		{
			.desc		= "TCP IPv6 redir port",
			.lookup_prog	= skel->progs.redir_port,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { EXT_IP6, INT_PORT },
		},
		{
			.desc		= "TCP IPv6 redir addr",
			.lookup_prog	= skel->progs.redir_ip6,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, EXT_PORT },
		},
		{
			.desc		= "TCP IPv4->IPv6 redir port",
			.lookup_prog	= skel->progs.redir_port,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4_V6, INT_PORT },
		},
		{
			.desc		= "TCP IPv6 redir with reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, INT_PORT },
			.accept_on	= SERVER_B,
		},
		{
			.desc		= "TCP IPv6 redir skip reuseport",
			.lookup_prog	= skel->progs.select_sock_a_no_reuseport,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, INT_PORT },
			.accept_on	= SERVER_A,
		},
		{
			.desc		= "UDP IPv4 redir port",
			.lookup_prog	= skel->progs.redir_port,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { EXT_IP4, INT_PORT },
		},
		{
			.desc		= "UDP IPv4 redir addr",
			.lookup_prog	= skel->progs.redir_ip4,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, EXT_PORT },
		},
		{
			.desc		= "UDP IPv4 redir with reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, INT_PORT },
			.accept_on	= SERVER_B,
		},
		{
			.desc		= "UDP IPv4 redir and reuseport with conns",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, INT_PORT },
			.accept_on	= SERVER_B,
			.reuseport_has_conns = true,
		},
		{
			.desc		= "UDP IPv4 redir skip reuseport",
			.lookup_prog	= skel->progs.select_sock_a_no_reuseport,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, INT_PORT },
			.accept_on	= SERVER_A,
		},
		{
			.desc		= "UDP IPv6 redir port",
			.lookup_prog	= skel->progs.redir_port,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { EXT_IP6, INT_PORT },
		},
		{
			.desc		= "UDP IPv6 redir addr",
			.lookup_prog	= skel->progs.redir_ip6,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, EXT_PORT },
		},
		{
			.desc		= "UDP IPv4->IPv6 redir port",
			.lookup_prog	= skel->progs.redir_port,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.listen_at	= { INT_IP4_V6, INT_PORT },
			.connect_to	= { EXT_IP4, EXT_PORT },
		},
		{
			.desc		= "UDP IPv6 redir and reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, INT_PORT },
			.accept_on	= SERVER_B,
		},
		{
			.desc		= "UDP IPv6 redir and reuseport with conns",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, INT_PORT },
			.accept_on	= SERVER_B,
			.reuseport_has_conns = true,
		},
		{
			.desc		= "UDP IPv6 redir skip reuseport",
			.lookup_prog	= skel->progs.select_sock_a_no_reuseport,
			.reuseport_prog	= skel->progs.select_sock_b,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, INT_PORT },
			.accept_on	= SERVER_A,
		},
	};
	const struct test *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		if (test__start_subtest(t->desc))
			run_lookup_prog(t);
	}
}

static void drop_on_lookup(const struct test *t)
{
	struct sockaddr_storage dst = {};
	int client_fd, server_fd, err;
	struct bpf_link *lookup_link;
	ssize_t n;

	lookup_link = attach_lookup_prog(t->lookup_prog);
	if (!lookup_link)
		return;

	server_fd = make_server(t->sotype, t->listen_at.ip, t->listen_at.port,
				t->reuseport_prog);
	if (server_fd < 0)
		goto detach;

	client_fd = make_socket(t->sotype, t->connect_to.ip,
				t->connect_to.port, &dst);
	if (client_fd < 0)
		goto close_srv;

	err = connect(client_fd, (void *)&dst, inetaddr_len(&dst));
	if (t->sotype == SOCK_DGRAM) {
		err = send_byte(client_fd);
		if (err)
			goto close_all;

		/* Read out asynchronous error */
		n = recv(client_fd, NULL, 0, 0);
		err = n == -1;
	}
	if (CHECK(!err || errno != ECONNREFUSED, "connect",
		  "unexpected success or error\n"))
		log_err("expected ECONNREFUSED on connect");

close_all:
	close(client_fd);
close_srv:
	close(server_fd);
detach:
	bpf_link__destroy(lookup_link);
}

static void test_drop_on_lookup(struct test_sk_lookup *skel)
{
	const struct test tests[] = {
		{
			.desc		= "TCP IPv4 drop on lookup",
			.lookup_prog	= skel->progs.lookup_drop,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { EXT_IP4, EXT_PORT },
		},
		{
			.desc		= "TCP IPv6 drop on lookup",
			.lookup_prog	= skel->progs.lookup_drop,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { EXT_IP6, EXT_PORT },
		},
		{
			.desc		= "UDP IPv4 drop on lookup",
			.lookup_prog	= skel->progs.lookup_drop,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { EXT_IP4, EXT_PORT },
		},
		{
			.desc		= "UDP IPv6 drop on lookup",
			.lookup_prog	= skel->progs.lookup_drop,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { EXT_IP6, INT_PORT },
		},
	};
	const struct test *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		if (test__start_subtest(t->desc))
			drop_on_lookup(t);
	}
}

static void drop_on_reuseport(const struct test *t)
{
	struct sockaddr_storage dst = { 0 };
	int client, server1, server2, err;
	struct bpf_link *lookup_link;
	ssize_t n;

	lookup_link = attach_lookup_prog(t->lookup_prog);
	if (!lookup_link)
		return;

	server1 = make_server(t->sotype, t->listen_at.ip, t->listen_at.port,
			      t->reuseport_prog);
	if (server1 < 0)
		goto detach;

	err = update_lookup_map(t->sock_map, SERVER_A, server1);
	if (err)
		goto detach;

	/* second server on destination address we should never reach */
	server2 = make_server(t->sotype, t->connect_to.ip, t->connect_to.port,
			      NULL /* reuseport prog */);
	if (server2 < 0)
		goto close_srv1;

	client = make_socket(t->sotype, t->connect_to.ip,
			     t->connect_to.port, &dst);
	if (client < 0)
		goto close_srv2;

	err = connect(client, (void *)&dst, inetaddr_len(&dst));
	if (t->sotype == SOCK_DGRAM) {
		err = send_byte(client);
		if (err)
			goto close_all;

		/* Read out asynchronous error */
		n = recv(client, NULL, 0, 0);
		err = n == -1;
	}
	if (CHECK(!err || errno != ECONNREFUSED, "connect",
		  "unexpected success or error\n"))
		log_err("expected ECONNREFUSED on connect");

close_all:
	close(client);
close_srv2:
	close(server2);
close_srv1:
	close(server1);
detach:
	bpf_link__destroy(lookup_link);
}

static void test_drop_on_reuseport(struct test_sk_lookup *skel)
{
	const struct test tests[] = {
		{
			.desc		= "TCP IPv4 drop on reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.reuseport_drop,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, INT_PORT },
		},
		{
			.desc		= "TCP IPv6 drop on reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.reuseport_drop,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, INT_PORT },
		},
		{
			.desc		= "UDP IPv4 drop on reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.reuseport_drop,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_DGRAM,
			.connect_to	= { EXT_IP4, EXT_PORT },
			.listen_at	= { INT_IP4, INT_PORT },
		},
		{
			.desc		= "TCP IPv6 drop on reuseport",
			.lookup_prog	= skel->progs.select_sock_a,
			.reuseport_prog	= skel->progs.reuseport_drop,
			.sock_map	= skel->maps.redir_map,
			.sotype		= SOCK_STREAM,
			.connect_to	= { EXT_IP6, EXT_PORT },
			.listen_at	= { INT_IP6, INT_PORT },
		},
	};
	const struct test *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		if (test__start_subtest(t->desc))
			drop_on_reuseport(t);
	}
}

static void run_sk_assign(struct test_sk_lookup *skel,
			  struct bpf_program *lookup_prog,
			  const char *remote_ip, const char *local_ip)
{
	int server_fds[MAX_SERVERS] = { -1 };
	struct bpf_sk_lookup ctx;
	__u64 server_cookie;
	int i, err;

	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
		.ctx_in = &ctx,
		.ctx_size_in = sizeof(ctx),
		.ctx_out = &ctx,
		.ctx_size_out = sizeof(ctx),
	);

	if (fill_sk_lookup_ctx(&ctx, local_ip, EXT_PORT, remote_ip, INT_PORT))
		return;

	ctx.protocol = IPPROTO_TCP;

	for (i = 0; i < ARRAY_SIZE(server_fds); i++) {
		server_fds[i] = make_server(SOCK_STREAM, local_ip, 0, NULL);
		if (server_fds[i] < 0)
			goto close_servers;

		err = update_lookup_map(skel->maps.redir_map, i,
					server_fds[i]);
		if (err)
			goto close_servers;
	}

	server_cookie = socket_cookie(server_fds[SERVER_B]);
	if (!server_cookie)
		return;

	err = bpf_prog_test_run_opts(bpf_program__fd(lookup_prog), &opts);
	if (CHECK(err, "test_run", "failed with error %d\n", errno))
		goto close_servers;

	if (CHECK(ctx.cookie == 0, "ctx.cookie", "no socket selected\n"))
		goto close_servers;

	CHECK(ctx.cookie != server_cookie, "ctx.cookie",
	      "selected sk %llu instead of %llu\n", ctx.cookie, server_cookie);

close_servers:
	for (i = 0; i < ARRAY_SIZE(server_fds); i++) {
		if (server_fds[i] != -1)
			close(server_fds[i]);
	}
}

static void run_sk_assign_v4(struct test_sk_lookup *skel,
			     struct bpf_program *lookup_prog)
{
	run_sk_assign(skel, lookup_prog, INT_IP4, EXT_IP4);
}

static void run_sk_assign_v6(struct test_sk_lookup *skel,
			     struct bpf_program *lookup_prog)
{
	run_sk_assign(skel, lookup_prog, INT_IP6, EXT_IP6);
}

static void run_sk_assign_connected(struct test_sk_lookup *skel,
				    int sotype)
{
	int err, client_fd, connected_fd, server_fd;
	struct bpf_link *lookup_link;

	server_fd = make_server(sotype, EXT_IP4, EXT_PORT, NULL);
	if (server_fd < 0)
		return;

	connected_fd = make_client(sotype, EXT_IP4, EXT_PORT);
	if (connected_fd < 0)
		goto out_close_server;

	/* Put a connected socket in redirect map */
	err = update_lookup_map(skel->maps.redir_map, SERVER_A, connected_fd);
	if (err)
		goto out_close_connected;

	lookup_link = attach_lookup_prog(skel->progs.sk_assign_esocknosupport);
	if (!lookup_link)
		goto out_close_connected;

	/* Try to redirect TCP SYN / UDP packet to a connected socket */
	client_fd = make_client(sotype, EXT_IP4, EXT_PORT);
	if (client_fd < 0)
		goto out_unlink_prog;
	if (sotype == SOCK_DGRAM) {
		send_byte(client_fd);
		recv_byte(server_fd);
	}

	close(client_fd);
out_unlink_prog:
	bpf_link__destroy(lookup_link);
out_close_connected:
	close(connected_fd);
out_close_server:
	close(server_fd);
}

static void test_sk_assign_helper(struct test_sk_lookup *skel)
{
	if (test__start_subtest("sk_assign returns EEXIST"))
		run_sk_assign_v4(skel, skel->progs.sk_assign_eexist);
	if (test__start_subtest("sk_assign honors F_REPLACE"))
		run_sk_assign_v4(skel, skel->progs.sk_assign_replace_flag);
	if (test__start_subtest("sk_assign accepts NULL socket"))
		run_sk_assign_v4(skel, skel->progs.sk_assign_null);
	if (test__start_subtest("access ctx->sk"))
		run_sk_assign_v4(skel, skel->progs.access_ctx_sk);
	if (test__start_subtest("narrow access to ctx v4"))
		run_sk_assign_v4(skel, skel->progs.ctx_narrow_access);
	if (test__start_subtest("narrow access to ctx v6"))
		run_sk_assign_v6(skel, skel->progs.ctx_narrow_access);
	if (test__start_subtest("sk_assign rejects TCP established"))
		run_sk_assign_connected(skel, SOCK_STREAM);
	if (test__start_subtest("sk_assign rejects UDP connected"))
		run_sk_assign_connected(skel, SOCK_DGRAM);
}

struct test_multi_prog {
	const char *desc;
	struct bpf_program *prog1;
	struct bpf_program *prog2;
	struct bpf_map *redir_map;
	struct bpf_map *run_map;
	int expect_errno;
	struct inet_addr listen_at;
};

static void run_multi_prog_lookup(const struct test_multi_prog *t)
{
	struct sockaddr_storage dst = {};
	int map_fd, server_fd, client_fd;
	struct bpf_link *link1, *link2;
	int prog_idx, done, err;

	map_fd = bpf_map__fd(t->run_map);

	done = 0;
	prog_idx = PROG1;
	err = bpf_map_update_elem(map_fd, &prog_idx, &done, BPF_ANY);
	if (CHECK(err, "bpf_map_update_elem", "failed\n"))
		return;
	prog_idx = PROG2;
	err = bpf_map_update_elem(map_fd, &prog_idx, &done, BPF_ANY);
	if (CHECK(err, "bpf_map_update_elem", "failed\n"))
		return;

	link1 = attach_lookup_prog(t->prog1);
	if (!link1)
		return;
	link2 = attach_lookup_prog(t->prog2);
	if (!link2)
		goto out_unlink1;

	server_fd = make_server(SOCK_STREAM, t->listen_at.ip,
				t->listen_at.port, NULL);
	if (server_fd < 0)
		goto out_unlink2;

	err = update_lookup_map(t->redir_map, SERVER_A, server_fd);
	if (err)
		goto out_close_server;

	client_fd = make_socket(SOCK_STREAM, EXT_IP4, EXT_PORT, &dst);
	if (client_fd < 0)
		goto out_close_server;

	err = connect(client_fd, (void *)&dst, inetaddr_len(&dst));
	if (CHECK(err && !t->expect_errno, "connect",
		  "unexpected error %d\n", errno))
		goto out_close_client;
	if (CHECK(err && t->expect_errno && errno != t->expect_errno,
		  "connect", "unexpected error %d\n", errno))
		goto out_close_client;

	done = 0;
	prog_idx = PROG1;
	err = bpf_map_lookup_elem(map_fd, &prog_idx, &done);
	CHECK(err, "bpf_map_lookup_elem", "failed\n");
	CHECK(!done, "bpf_map_lookup_elem", "PROG1 !done\n");

	done = 0;
	prog_idx = PROG2;
	err = bpf_map_lookup_elem(map_fd, &prog_idx, &done);
	CHECK(err, "bpf_map_lookup_elem", "failed\n");
	CHECK(!done, "bpf_map_lookup_elem", "PROG2 !done\n");

out_close_client:
	close(client_fd);
out_close_server:
	close(server_fd);
out_unlink2:
	bpf_link__destroy(link2);
out_unlink1:
	bpf_link__destroy(link1);
}

static void test_multi_prog_lookup(struct test_sk_lookup *skel)
{
	struct test_multi_prog tests[] = {
		{
			.desc		= "multi prog - pass, pass",
			.prog1		= skel->progs.multi_prog_pass1,
			.prog2		= skel->progs.multi_prog_pass2,
			.listen_at	= { EXT_IP4, EXT_PORT },
		},
		{
			.desc		= "multi prog - drop, drop",
			.prog1		= skel->progs.multi_prog_drop1,
			.prog2		= skel->progs.multi_prog_drop2,
			.listen_at	= { EXT_IP4, EXT_PORT },
			.expect_errno	= ECONNREFUSED,
		},
		{
			.desc		= "multi prog - pass, drop",
			.prog1		= skel->progs.multi_prog_pass1,
			.prog2		= skel->progs.multi_prog_drop2,
			.listen_at	= { EXT_IP4, EXT_PORT },
			.expect_errno	= ECONNREFUSED,
		},
		{
			.desc		= "multi prog - drop, pass",
			.prog1		= skel->progs.multi_prog_drop1,
			.prog2		= skel->progs.multi_prog_pass2,
			.listen_at	= { EXT_IP4, EXT_PORT },
			.expect_errno	= ECONNREFUSED,
		},
		{
			.desc		= "multi prog - pass, redir",
			.prog1		= skel->progs.multi_prog_pass1,
			.prog2		= skel->progs.multi_prog_redir2,
			.listen_at	= { INT_IP4, INT_PORT },
		},
		{
			.desc		= "multi prog - redir, pass",
			.prog1		= skel->progs.multi_prog_redir1,
			.prog2		= skel->progs.multi_prog_pass2,
			.listen_at	= { INT_IP4, INT_PORT },
		},
		{
			.desc		= "multi prog - drop, redir",
			.prog1		= skel->progs.multi_prog_drop1,
			.prog2		= skel->progs.multi_prog_redir2,
			.listen_at	= { INT_IP4, INT_PORT },
		},
		{
			.desc		= "multi prog - redir, drop",
			.prog1		= skel->progs.multi_prog_redir1,
			.prog2		= skel->progs.multi_prog_drop2,
			.listen_at	= { INT_IP4, INT_PORT },
		},
		{
			.desc		= "multi prog - redir, redir",
			.prog1		= skel->progs.multi_prog_redir1,
			.prog2		= skel->progs.multi_prog_redir2,
			.listen_at	= { INT_IP4, INT_PORT },
		},
	};
	struct test_multi_prog *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		t->redir_map = skel->maps.redir_map;
		t->run_map = skel->maps.run_map;
		if (test__start_subtest(t->desc))
			run_multi_prog_lookup(t);
	}
}

static void run_tests(struct test_sk_lookup *skel)
{
	if (test__start_subtest("query lookup prog"))
		query_lookup_prog(skel);
	test_redirect_lookup(skel);
	test_drop_on_lookup(skel);
	test_drop_on_reuseport(skel);
	test_sk_assign_helper(skel);
	test_multi_prog_lookup(skel);
}

static int switch_netns(void)
{
	static const char * const setup_script[] = {
		"ip -6 addr add dev lo " EXT_IP6 "/128",
		"ip -6 addr add dev lo " INT_IP6 "/128",
		"ip link set dev lo up",
		NULL,
	};
	const char * const *cmd;
	int err;

	err = unshare(CLONE_NEWNET);
	if (CHECK(err, "unshare", "failed\n")) {
		log_err("unshare(CLONE_NEWNET)");
		return -1;
	}

	for (cmd = setup_script; *cmd; cmd++) {
		err = system(*cmd);
		if (CHECK(err, "system", "failed\n")) {
			log_err("system(%s)", *cmd);
			return -1;
		}
	}

	return 0;
}

void test_sk_lookup(void)
{
	struct test_sk_lookup *skel;
	int err;

	err = switch_netns();
	if (err)
		return;

	skel = test_sk_lookup__open_and_load();
	if (CHECK(!skel, "skel open_and_load", "failed\n"))
		return;

	run_tests(skel);

	test_sk_lookup__destroy(skel);
}
