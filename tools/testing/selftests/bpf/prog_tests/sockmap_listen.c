// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
/*
 * Test suite for SOCKMAP/SOCKHASH holding listening sockets.
 * Covers:
 *  1. BPF map operations - bpf_map_{update,lookup delete}_elem
 *  2. BPF redirect helpers - bpf_{sk,msg}_redirect_map
 *  3. BPF reuseport helper - bpf_sk_select_reuseport
 */

#include <linux/compiler.h>
#include <errno.h>
#include <error.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_util.h"
#include "test_progs.h"
#include "test_sockmap_listen.skel.h"

#define IO_TIMEOUT_SEC 30
#define MAX_STRERR_LEN 256
#define MAX_TEST_NAME 80

#define _FAIL(errnum, fmt...)                                                  \
	({                                                                     \
		error_at_line(0, (errnum), __func__, __LINE__, fmt);           \
		CHECK_FAIL(true);                                              \
	})
#define FAIL(fmt...) _FAIL(0, fmt)
#define FAIL_ERRNO(fmt...) _FAIL(errno, fmt)
#define FAIL_LIBBPF(err, msg)                                                  \
	({                                                                     \
		char __buf[MAX_STRERR_LEN];                                    \
		libbpf_strerror((err), __buf, sizeof(__buf));                  \
		FAIL("%s: %s", (msg), __buf);                                  \
	})

/* Wrappers that fail the test on error and report it. */

#define xaccept_nonblock(fd, addr, len)                                        \
	({                                                                     \
		int __ret =                                                    \
			accept_timeout((fd), (addr), (len), IO_TIMEOUT_SEC);   \
		if (__ret == -1)                                               \
			FAIL_ERRNO("accept");                                  \
		__ret;                                                         \
	})

#define xbind(fd, addr, len)                                                   \
	({                                                                     \
		int __ret = bind((fd), (addr), (len));                         \
		if (__ret == -1)                                               \
			FAIL_ERRNO("bind");                                    \
		__ret;                                                         \
	})

#define xclose(fd)                                                             \
	({                                                                     \
		int __ret = close((fd));                                       \
		if (__ret == -1)                                               \
			FAIL_ERRNO("close");                                   \
		__ret;                                                         \
	})

#define xconnect(fd, addr, len)                                                \
	({                                                                     \
		int __ret = connect((fd), (addr), (len));                      \
		if (__ret == -1)                                               \
			FAIL_ERRNO("connect");                                 \
		__ret;                                                         \
	})

#define xgetsockname(fd, addr, len)                                            \
	({                                                                     \
		int __ret = getsockname((fd), (addr), (len));                  \
		if (__ret == -1)                                               \
			FAIL_ERRNO("getsockname");                             \
		__ret;                                                         \
	})

#define xgetsockopt(fd, level, name, val, len)                                 \
	({                                                                     \
		int __ret = getsockopt((fd), (level), (name), (val), (len));   \
		if (__ret == -1)                                               \
			FAIL_ERRNO("getsockopt(" #name ")");                   \
		__ret;                                                         \
	})

#define xlisten(fd, backlog)                                                   \
	({                                                                     \
		int __ret = listen((fd), (backlog));                           \
		if (__ret == -1)                                               \
			FAIL_ERRNO("listen");                                  \
		__ret;                                                         \
	})

#define xsetsockopt(fd, level, name, val, len)                                 \
	({                                                                     \
		int __ret = setsockopt((fd), (level), (name), (val), (len));   \
		if (__ret == -1)                                               \
			FAIL_ERRNO("setsockopt(" #name ")");                   \
		__ret;                                                         \
	})

#define xsend(fd, buf, len, flags)                                             \
	({                                                                     \
		ssize_t __ret = send((fd), (buf), (len), (flags));             \
		if (__ret == -1)                                               \
			FAIL_ERRNO("send");                                    \
		__ret;                                                         \
	})

#define xrecv_nonblock(fd, buf, len, flags)                                    \
	({                                                                     \
		ssize_t __ret = recv_timeout((fd), (buf), (len), (flags),      \
					     IO_TIMEOUT_SEC);                  \
		if (__ret == -1)                                               \
			FAIL_ERRNO("recv");                                    \
		__ret;                                                         \
	})

#define xsocket(family, sotype, flags)                                         \
	({                                                                     \
		int __ret = socket(family, sotype, flags);                     \
		if (__ret == -1)                                               \
			FAIL_ERRNO("socket");                                  \
		__ret;                                                         \
	})

#define xbpf_map_delete_elem(fd, key)                                          \
	({                                                                     \
		int __ret = bpf_map_delete_elem((fd), (key));                  \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_delete");                              \
		__ret;                                                         \
	})

#define xbpf_map_lookup_elem(fd, key, val)                                     \
	({                                                                     \
		int __ret = bpf_map_lookup_elem((fd), (key), (val));           \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_lookup");                              \
		__ret;                                                         \
	})

#define xbpf_map_update_elem(fd, key, val, flags)                              \
	({                                                                     \
		int __ret = bpf_map_update_elem((fd), (key), (val), (flags));  \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_update");                              \
		__ret;                                                         \
	})

#define xbpf_prog_attach(prog, target, type, flags)                            \
	({                                                                     \
		int __ret =                                                    \
			bpf_prog_attach((prog), (target), (type), (flags));    \
		if (__ret < 0)                                               \
			FAIL_ERRNO("prog_attach(" #type ")");                  \
		__ret;                                                         \
	})

#define xbpf_prog_detach2(prog, target, type)                                  \
	({                                                                     \
		int __ret = bpf_prog_detach2((prog), (target), (type));        \
		if (__ret < 0)                                               \
			FAIL_ERRNO("prog_detach2(" #type ")");                 \
		__ret;                                                         \
	})

#define xpthread_create(thread, attr, func, arg)                               \
	({                                                                     \
		int __ret = pthread_create((thread), (attr), (func), (arg));   \
		errno = __ret;                                                 \
		if (__ret)                                                     \
			FAIL_ERRNO("pthread_create");                          \
		__ret;                                                         \
	})

#define xpthread_join(thread, retval)                                          \
	({                                                                     \
		int __ret = pthread_join((thread), (retval));                  \
		errno = __ret;                                                 \
		if (__ret)                                                     \
			FAIL_ERRNO("pthread_join");                            \
		__ret;                                                         \
	})

static int poll_read(int fd, unsigned int timeout_sec)
{
	struct timeval timeout = { .tv_sec = timeout_sec };
	fd_set rfds;
	int r;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	r = select(fd + 1, &rfds, NULL, NULL, &timeout);
	if (r == 0)
		errno = ETIME;

	return r == 1 ? 0 : -1;
}

static int accept_timeout(int fd, struct sockaddr *addr, socklen_t *len,
			  unsigned int timeout_sec)
{
	if (poll_read(fd, timeout_sec))
		return -1;

	return accept(fd, addr, len);
}

static int recv_timeout(int fd, void *buf, size_t len, int flags,
			unsigned int timeout_sec)
{
	if (poll_read(fd, timeout_sec))
		return -1;

	return recv(fd, buf, len, flags);
}

static void init_addr_loopback4(struct sockaddr_storage *ss, socklen_t *len)
{
	struct sockaddr_in *addr4 = memset(ss, 0, sizeof(*ss));

	addr4->sin_family = AF_INET;
	addr4->sin_port = 0;
	addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	*len = sizeof(*addr4);
}

static void init_addr_loopback6(struct sockaddr_storage *ss, socklen_t *len)
{
	struct sockaddr_in6 *addr6 = memset(ss, 0, sizeof(*ss));

	addr6->sin6_family = AF_INET6;
	addr6->sin6_port = 0;
	addr6->sin6_addr = in6addr_loopback;
	*len = sizeof(*addr6);
}

static void init_addr_loopback(int family, struct sockaddr_storage *ss,
			       socklen_t *len)
{
	switch (family) {
	case AF_INET:
		init_addr_loopback4(ss, len);
		return;
	case AF_INET6:
		init_addr_loopback6(ss, len);
		return;
	default:
		FAIL("unsupported address family %d", family);
	}
}

static inline struct sockaddr *sockaddr(struct sockaddr_storage *ss)
{
	return (struct sockaddr *)ss;
}

static int enable_reuseport(int s, int progfd)
{
	int err, one = 1;

	err = xsetsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
	if (err)
		return -1;
	err = xsetsockopt(s, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &progfd,
			  sizeof(progfd));
	if (err)
		return -1;

	return 0;
}

static int socket_loopback_reuseport(int family, int sotype, int progfd)
{
	struct sockaddr_storage addr;
	socklen_t len;
	int err, s;

	init_addr_loopback(family, &addr, &len);

	s = xsocket(family, sotype, 0);
	if (s == -1)
		return -1;

	if (progfd >= 0)
		enable_reuseport(s, progfd);

	err = xbind(s, sockaddr(&addr), len);
	if (err)
		goto close;

	if (sotype & SOCK_DGRAM)
		return s;

	err = xlisten(s, SOMAXCONN);
	if (err)
		goto close;

	return s;
close:
	xclose(s);
	return -1;
}

static int socket_loopback(int family, int sotype)
{
	return socket_loopback_reuseport(family, sotype, -1);
}

static void test_insert_invalid(int family, int sotype, int mapfd)
{
	u32 key = 0;
	u64 value;
	int err;

	value = -1;
	err = bpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	if (!err || errno != EINVAL)
		FAIL_ERRNO("map_update: expected EINVAL");

	value = INT_MAX;
	err = bpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	if (!err || errno != EBADF)
		FAIL_ERRNO("map_update: expected EBADF");
}

static void test_insert_opened(int family, int sotype, int mapfd)
{
	u32 key = 0;
	u64 value;
	int err, s;

	s = xsocket(family, sotype, 0);
	if (s == -1)
		return;

	errno = 0;
	value = s;
	err = bpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	if (sotype == SOCK_STREAM) {
		if (!err || errno != EOPNOTSUPP)
			FAIL_ERRNO("map_update: expected EOPNOTSUPP");
	} else if (err)
		FAIL_ERRNO("map_update: expected success");
	xclose(s);
}

static void test_insert_bound(int family, int sotype, int mapfd)
{
	struct sockaddr_storage addr;
	socklen_t len;
	u32 key = 0;
	u64 value;
	int err, s;

	init_addr_loopback(family, &addr, &len);

	s = xsocket(family, sotype, 0);
	if (s == -1)
		return;

	err = xbind(s, sockaddr(&addr), len);
	if (err)
		goto close;

	errno = 0;
	value = s;
	err = bpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	if (!err || errno != EOPNOTSUPP)
		FAIL_ERRNO("map_update: expected EOPNOTSUPP");
close:
	xclose(s);
}

static void test_insert(int family, int sotype, int mapfd)
{
	u64 value;
	u32 key;
	int s;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	key = 0;
	value = s;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	xclose(s);
}

static void test_delete_after_insert(int family, int sotype, int mapfd)
{
	u64 value;
	u32 key;
	int s;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	key = 0;
	value = s;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	xbpf_map_delete_elem(mapfd, &key);
	xclose(s);
}

static void test_delete_after_close(int family, int sotype, int mapfd)
{
	int err, s;
	u64 value;
	u32 key;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	key = 0;
	value = s;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);

	xclose(s);

	errno = 0;
	err = bpf_map_delete_elem(mapfd, &key);
	if (!err || (errno != EINVAL && errno != ENOENT))
		/* SOCKMAP and SOCKHASH return different error codes */
		FAIL_ERRNO("map_delete: expected EINVAL/EINVAL");
}

static void test_lookup_after_insert(int family, int sotype, int mapfd)
{
	u64 cookie, value;
	socklen_t len;
	u32 key;
	int s;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	key = 0;
	value = s;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);

	len = sizeof(cookie);
	xgetsockopt(s, SOL_SOCKET, SO_COOKIE, &cookie, &len);

	xbpf_map_lookup_elem(mapfd, &key, &value);

	if (value != cookie) {
		FAIL("map_lookup: have %#llx, want %#llx",
		     (unsigned long long)value, (unsigned long long)cookie);
	}

	xclose(s);
}

static void test_lookup_after_delete(int family, int sotype, int mapfd)
{
	int err, s;
	u64 value;
	u32 key;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	key = 0;
	value = s;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	xbpf_map_delete_elem(mapfd, &key);

	errno = 0;
	err = bpf_map_lookup_elem(mapfd, &key, &value);
	if (!err || errno != ENOENT)
		FAIL_ERRNO("map_lookup: expected ENOENT");

	xclose(s);
}

static void test_lookup_32_bit_value(int family, int sotype, int mapfd)
{
	u32 key, value32;
	int err, s;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	mapfd = bpf_create_map(BPF_MAP_TYPE_SOCKMAP, sizeof(key),
			       sizeof(value32), 1, 0);
	if (mapfd < 0) {
		FAIL_ERRNO("map_create");
		goto close;
	}

	key = 0;
	value32 = s;
	xbpf_map_update_elem(mapfd, &key, &value32, BPF_NOEXIST);

	errno = 0;
	err = bpf_map_lookup_elem(mapfd, &key, &value32);
	if (!err || errno != ENOSPC)
		FAIL_ERRNO("map_lookup: expected ENOSPC");

	xclose(mapfd);
close:
	xclose(s);
}

static void test_update_existing(int family, int sotype, int mapfd)
{
	int s1, s2;
	u64 value;
	u32 key;

	s1 = socket_loopback(family, sotype);
	if (s1 < 0)
		return;

	s2 = socket_loopback(family, sotype);
	if (s2 < 0)
		goto close_s1;

	key = 0;
	value = s1;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);

	value = s2;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_EXIST);
	xclose(s2);
close_s1:
	xclose(s1);
}

/* Exercise the code path where we destroy child sockets that never
 * got accept()'ed, aka orphans, when parent socket gets closed.
 */
static void test_destroy_orphan_child(int family, int sotype, int mapfd)
{
	struct sockaddr_storage addr;
	socklen_t len;
	int err, s, c;
	u64 value;
	u32 key;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	key = 0;
	value = s;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);

	c = xsocket(family, sotype, 0);
	if (c == -1)
		goto close_srv;

	xconnect(c, sockaddr(&addr), len);
	xclose(c);
close_srv:
	xclose(s);
}

/* Perform a passive open after removing listening socket from SOCKMAP
 * to ensure that callbacks get restored properly.
 */
static void test_clone_after_delete(int family, int sotype, int mapfd)
{
	struct sockaddr_storage addr;
	socklen_t len;
	int err, s, c;
	u64 value;
	u32 key;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	key = 0;
	value = s;
	xbpf_map_update_elem(mapfd, &key, &value, BPF_NOEXIST);
	xbpf_map_delete_elem(mapfd, &key);

	c = xsocket(family, sotype, 0);
	if (c < 0)
		goto close_srv;

	xconnect(c, sockaddr(&addr), len);
	xclose(c);
close_srv:
	xclose(s);
}

/* Check that child socket that got created while parent was in a
 * SOCKMAP, but got accept()'ed only after the parent has been removed
 * from SOCKMAP, gets cloned without parent psock state or callbacks.
 */
static void test_accept_after_delete(int family, int sotype, int mapfd)
{
	struct sockaddr_storage addr;
	const u32 zero = 0;
	int err, s, c, p;
	socklen_t len;
	u64 value;

	s = socket_loopback(family, sotype | SOCK_NONBLOCK);
	if (s == -1)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	value = s;
	err = xbpf_map_update_elem(mapfd, &zero, &value, BPF_NOEXIST);
	if (err)
		goto close_srv;

	c = xsocket(family, sotype, 0);
	if (c == -1)
		goto close_srv;

	/* Create child while parent is in sockmap */
	err = xconnect(c, sockaddr(&addr), len);
	if (err)
		goto close_cli;

	/* Remove parent from sockmap */
	err = xbpf_map_delete_elem(mapfd, &zero);
	if (err)
		goto close_cli;

	p = xaccept_nonblock(s, NULL, NULL);
	if (p == -1)
		goto close_cli;

	/* Check that child sk_user_data is not set */
	value = p;
	xbpf_map_update_elem(mapfd, &zero, &value, BPF_NOEXIST);

	xclose(p);
close_cli:
	xclose(c);
close_srv:
	xclose(s);
}

/* Check that child socket that got created and accepted while parent
 * was in a SOCKMAP is cloned without parent psock state or callbacks.
 */
static void test_accept_before_delete(int family, int sotype, int mapfd)
{
	struct sockaddr_storage addr;
	const u32 zero = 0, one = 1;
	int err, s, c, p;
	socklen_t len;
	u64 value;

	s = socket_loopback(family, sotype | SOCK_NONBLOCK);
	if (s == -1)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	value = s;
	err = xbpf_map_update_elem(mapfd, &zero, &value, BPF_NOEXIST);
	if (err)
		goto close_srv;

	c = xsocket(family, sotype, 0);
	if (c == -1)
		goto close_srv;

	/* Create & accept child while parent is in sockmap */
	err = xconnect(c, sockaddr(&addr), len);
	if (err)
		goto close_cli;

	p = xaccept_nonblock(s, NULL, NULL);
	if (p == -1)
		goto close_cli;

	/* Check that child sk_user_data is not set */
	value = p;
	xbpf_map_update_elem(mapfd, &one, &value, BPF_NOEXIST);

	xclose(p);
close_cli:
	xclose(c);
close_srv:
	xclose(s);
}

struct connect_accept_ctx {
	int sockfd;
	unsigned int done;
	unsigned int nr_iter;
};

static bool is_thread_done(struct connect_accept_ctx *ctx)
{
	return READ_ONCE(ctx->done);
}

static void *connect_accept_thread(void *arg)
{
	struct connect_accept_ctx *ctx = arg;
	struct sockaddr_storage addr;
	int family, socktype;
	socklen_t len;
	int err, i, s;

	s = ctx->sockfd;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto done;

	len = sizeof(family);
	err = xgetsockopt(s, SOL_SOCKET, SO_DOMAIN, &family, &len);
	if (err)
		goto done;

	len = sizeof(socktype);
	err = xgetsockopt(s, SOL_SOCKET, SO_TYPE, &socktype, &len);
	if (err)
		goto done;

	for (i = 0; i < ctx->nr_iter; i++) {
		int c, p;

		c = xsocket(family, socktype, 0);
		if (c < 0)
			break;

		err = xconnect(c, (struct sockaddr *)&addr, sizeof(addr));
		if (err) {
			xclose(c);
			break;
		}

		p = xaccept_nonblock(s, NULL, NULL);
		if (p < 0) {
			xclose(c);
			break;
		}

		xclose(p);
		xclose(c);
	}
done:
	WRITE_ONCE(ctx->done, 1);
	return NULL;
}

static void test_syn_recv_insert_delete(int family, int sotype, int mapfd)
{
	struct connect_accept_ctx ctx = { 0 };
	struct sockaddr_storage addr;
	socklen_t len;
	u32 zero = 0;
	pthread_t t;
	int err, s;
	u64 value;

	s = socket_loopback(family, sotype | SOCK_NONBLOCK);
	if (s < 0)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close;

	ctx.sockfd = s;
	ctx.nr_iter = 1000;

	err = xpthread_create(&t, NULL, connect_accept_thread, &ctx);
	if (err)
		goto close;

	value = s;
	while (!is_thread_done(&ctx)) {
		err = xbpf_map_update_elem(mapfd, &zero, &value, BPF_NOEXIST);
		if (err)
			break;

		err = xbpf_map_delete_elem(mapfd, &zero);
		if (err)
			break;
	}

	xpthread_join(t, NULL);
close:
	xclose(s);
}

static void *listen_thread(void *arg)
{
	struct sockaddr unspec = { AF_UNSPEC };
	struct connect_accept_ctx *ctx = arg;
	int err, i, s;

	s = ctx->sockfd;

	for (i = 0; i < ctx->nr_iter; i++) {
		err = xlisten(s, 1);
		if (err)
			break;
		err = xconnect(s, &unspec, sizeof(unspec));
		if (err)
			break;
	}

	WRITE_ONCE(ctx->done, 1);
	return NULL;
}

static void test_race_insert_listen(int family, int socktype, int mapfd)
{
	struct connect_accept_ctx ctx = { 0 };
	const u32 zero = 0;
	const int one = 1;
	pthread_t t;
	int err, s;
	u64 value;

	s = xsocket(family, socktype, 0);
	if (s < 0)
		return;

	err = xsetsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (err)
		goto close;

	ctx.sockfd = s;
	ctx.nr_iter = 10000;

	err = pthread_create(&t, NULL, listen_thread, &ctx);
	if (err)
		goto close;

	value = s;
	while (!is_thread_done(&ctx)) {
		err = bpf_map_update_elem(mapfd, &zero, &value, BPF_NOEXIST);
		/* Expecting EOPNOTSUPP before listen() */
		if (err && errno != EOPNOTSUPP) {
			FAIL_ERRNO("map_update");
			break;
		}

		err = bpf_map_delete_elem(mapfd, &zero);
		/* Expecting no entry after unhash on connect(AF_UNSPEC) */
		if (err && errno != EINVAL && errno != ENOENT) {
			FAIL_ERRNO("map_delete");
			break;
		}
	}

	xpthread_join(t, NULL);
close:
	xclose(s);
}

static void zero_verdict_count(int mapfd)
{
	unsigned int zero = 0;
	int key;

	key = SK_DROP;
	xbpf_map_update_elem(mapfd, &key, &zero, BPF_ANY);
	key = SK_PASS;
	xbpf_map_update_elem(mapfd, &key, &zero, BPF_ANY);
}

enum redir_mode {
	REDIR_INGRESS,
	REDIR_EGRESS,
};

static const char *redir_mode_str(enum redir_mode mode)
{
	switch (mode) {
	case REDIR_INGRESS:
		return "ingress";
	case REDIR_EGRESS:
		return "egress";
	default:
		return "unknown";
	}
}

static int add_to_sockmap(int sock_mapfd, int fd1, int fd2)
{
	u64 value;
	u32 key;
	int err;

	key = 0;
	value = fd1;
	err = xbpf_map_update_elem(sock_mapfd, &key, &value, BPF_NOEXIST);
	if (err)
		return err;

	key = 1;
	value = fd2;
	return xbpf_map_update_elem(sock_mapfd, &key, &value, BPF_NOEXIST);
}

static void redir_to_connected(int family, int sotype, int sock_mapfd,
			       int verd_mapfd, enum redir_mode mode)
{
	const char *log_prefix = redir_mode_str(mode);
	struct sockaddr_storage addr;
	int s, c0, c1, p0, p1;
	unsigned int pass;
	socklen_t len;
	int err, n;
	u32 key;
	char b;

	zero_verdict_count(verd_mapfd);

	s = socket_loopback(family, sotype | SOCK_NONBLOCK);
	if (s < 0)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	c0 = xsocket(family, sotype, 0);
	if (c0 < 0)
		goto close_srv;
	err = xconnect(c0, sockaddr(&addr), len);
	if (err)
		goto close_cli0;

	p0 = xaccept_nonblock(s, NULL, NULL);
	if (p0 < 0)
		goto close_cli0;

	c1 = xsocket(family, sotype, 0);
	if (c1 < 0)
		goto close_peer0;
	err = xconnect(c1, sockaddr(&addr), len);
	if (err)
		goto close_cli1;

	p1 = xaccept_nonblock(s, NULL, NULL);
	if (p1 < 0)
		goto close_cli1;

	err = add_to_sockmap(sock_mapfd, p0, p1);
	if (err)
		goto close_peer1;

	n = write(mode == REDIR_INGRESS ? c1 : p1, "a", 1);
	if (n < 0)
		FAIL_ERRNO("%s: write", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete write", log_prefix);
	if (n < 1)
		goto close_peer1;

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &pass);
	if (err)
		goto close_peer1;
	if (pass != 1)
		FAIL("%s: want pass count 1, have %d", log_prefix, pass);

	n = read(c0, &b, 1);
	if (n < 0)
		FAIL_ERRNO("%s: read", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete read", log_prefix);

close_peer1:
	xclose(p1);
close_cli1:
	xclose(c1);
close_peer0:
	xclose(p0);
close_cli0:
	xclose(c0);
close_srv:
	xclose(s);
}

static void test_skb_redir_to_connected(struct test_sockmap_listen *skel,
					struct bpf_map *inner_map, int family,
					int sotype)
{
	int verdict = bpf_program__fd(skel->progs.prog_stream_verdict);
	int parser = bpf_program__fd(skel->progs.prog_stream_parser);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(parser, sock_map, BPF_SK_SKB_STREAM_PARSER, 0);
	if (err)
		return;
	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err)
		goto detach;

	redir_to_connected(family, sotype, sock_map, verdict_map,
			   REDIR_INGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_STREAM_VERDICT);
detach:
	xbpf_prog_detach2(parser, sock_map, BPF_SK_SKB_STREAM_PARSER);
}

static void test_msg_redir_to_connected(struct test_sockmap_listen *skel,
					struct bpf_map *inner_map, int family,
					int sotype)
{
	int verdict = bpf_program__fd(skel->progs.prog_msg_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_MSG_VERDICT, 0);
	if (err)
		return;

	redir_to_connected(family, sotype, sock_map, verdict_map, REDIR_EGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_MSG_VERDICT);
}

static void redir_to_listening(int family, int sotype, int sock_mapfd,
			       int verd_mapfd, enum redir_mode mode)
{
	const char *log_prefix = redir_mode_str(mode);
	struct sockaddr_storage addr;
	int s, c, p, err, n;
	unsigned int drop;
	socklen_t len;
	u32 key;

	zero_verdict_count(verd_mapfd);

	s = socket_loopback(family, sotype | SOCK_NONBLOCK);
	if (s < 0)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	c = xsocket(family, sotype, 0);
	if (c < 0)
		goto close_srv;
	err = xconnect(c, sockaddr(&addr), len);
	if (err)
		goto close_cli;

	p = xaccept_nonblock(s, NULL, NULL);
	if (p < 0)
		goto close_cli;

	err = add_to_sockmap(sock_mapfd, s, p);
	if (err)
		goto close_peer;

	n = write(mode == REDIR_INGRESS ? c : p, "a", 1);
	if (n < 0 && errno != EACCES)
		FAIL_ERRNO("%s: write", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete write", log_prefix);
	if (n < 1)
		goto close_peer;

	key = SK_DROP;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &drop);
	if (err)
		goto close_peer;
	if (drop != 1)
		FAIL("%s: want drop count 1, have %d", log_prefix, drop);

close_peer:
	xclose(p);
close_cli:
	xclose(c);
close_srv:
	xclose(s);
}

static void test_skb_redir_to_listening(struct test_sockmap_listen *skel,
					struct bpf_map *inner_map, int family,
					int sotype)
{
	int verdict = bpf_program__fd(skel->progs.prog_stream_verdict);
	int parser = bpf_program__fd(skel->progs.prog_stream_parser);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(parser, sock_map, BPF_SK_SKB_STREAM_PARSER, 0);
	if (err)
		return;
	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err)
		goto detach;

	redir_to_listening(family, sotype, sock_map, verdict_map,
			   REDIR_INGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_STREAM_VERDICT);
detach:
	xbpf_prog_detach2(parser, sock_map, BPF_SK_SKB_STREAM_PARSER);
}

static void test_msg_redir_to_listening(struct test_sockmap_listen *skel,
					struct bpf_map *inner_map, int family,
					int sotype)
{
	int verdict = bpf_program__fd(skel->progs.prog_msg_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_MSG_VERDICT, 0);
	if (err)
		return;

	redir_to_listening(family, sotype, sock_map, verdict_map, REDIR_EGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_MSG_VERDICT);
}

static void test_reuseport_select_listening(int family, int sotype,
					    int sock_map, int verd_map,
					    int reuseport_prog)
{
	struct sockaddr_storage addr;
	unsigned int pass;
	int s, c, err;
	socklen_t len;
	u64 value;
	u32 key;

	zero_verdict_count(verd_map);

	s = socket_loopback_reuseport(family, sotype | SOCK_NONBLOCK,
				      reuseport_prog);
	if (s < 0)
		return;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	key = 0;
	value = s;
	err = xbpf_map_update_elem(sock_map, &key, &value, BPF_NOEXIST);
	if (err)
		goto close_srv;

	c = xsocket(family, sotype, 0);
	if (c < 0)
		goto close_srv;
	err = xconnect(c, sockaddr(&addr), len);
	if (err)
		goto close_cli;

	if (sotype == SOCK_STREAM) {
		int p;

		p = xaccept_nonblock(s, NULL, NULL);
		if (p < 0)
			goto close_cli;
		xclose(p);
	} else {
		char b = 'a';
		ssize_t n;

		n = xsend(c, &b, sizeof(b), 0);
		if (n == -1)
			goto close_cli;

		n = xrecv_nonblock(s, &b, sizeof(b), 0);
		if (n == -1)
			goto close_cli;
	}

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_map, &key, &pass);
	if (err)
		goto close_cli;
	if (pass != 1)
		FAIL("want pass count 1, have %d", pass);

close_cli:
	xclose(c);
close_srv:
	xclose(s);
}

static void test_reuseport_select_connected(int family, int sotype,
					    int sock_map, int verd_map,
					    int reuseport_prog)
{
	struct sockaddr_storage addr;
	int s, c0, c1, p0, err;
	unsigned int drop;
	socklen_t len;
	u64 value;
	u32 key;

	zero_verdict_count(verd_map);

	s = socket_loopback_reuseport(family, sotype, reuseport_prog);
	if (s < 0)
		return;

	/* Populate sock_map[0] to avoid ENOENT on first connection */
	key = 0;
	value = s;
	err = xbpf_map_update_elem(sock_map, &key, &value, BPF_NOEXIST);
	if (err)
		goto close_srv;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	c0 = xsocket(family, sotype, 0);
	if (c0 < 0)
		goto close_srv;

	err = xconnect(c0, sockaddr(&addr), len);
	if (err)
		goto close_cli0;

	if (sotype == SOCK_STREAM) {
		p0 = xaccept_nonblock(s, NULL, NULL);
		if (p0 < 0)
			goto close_cli0;
	} else {
		p0 = xsocket(family, sotype, 0);
		if (p0 < 0)
			goto close_cli0;

		len = sizeof(addr);
		err = xgetsockname(c0, sockaddr(&addr), &len);
		if (err)
			goto close_cli0;

		err = xconnect(p0, sockaddr(&addr), len);
		if (err)
			goto close_cli0;
	}

	/* Update sock_map[0] to redirect to a connected socket */
	key = 0;
	value = p0;
	err = xbpf_map_update_elem(sock_map, &key, &value, BPF_EXIST);
	if (err)
		goto close_peer0;

	c1 = xsocket(family, sotype, 0);
	if (c1 < 0)
		goto close_peer0;

	len = sizeof(addr);
	err = xgetsockname(s, sockaddr(&addr), &len);
	if (err)
		goto close_srv;

	errno = 0;
	err = connect(c1, sockaddr(&addr), len);
	if (sotype == SOCK_DGRAM) {
		char b = 'a';
		ssize_t n;

		n = xsend(c1, &b, sizeof(b), 0);
		if (n == -1)
			goto close_cli1;

		n = recv_timeout(c1, &b, sizeof(b), 0, IO_TIMEOUT_SEC);
		err = n == -1;
	}
	if (!err || errno != ECONNREFUSED)
		FAIL_ERRNO("connect: expected ECONNREFUSED");

	key = SK_DROP;
	err = xbpf_map_lookup_elem(verd_map, &key, &drop);
	if (err)
		goto close_cli1;
	if (drop != 1)
		FAIL("want drop count 1, have %d", drop);

close_cli1:
	xclose(c1);
close_peer0:
	xclose(p0);
close_cli0:
	xclose(c0);
close_srv:
	xclose(s);
}

/* Check that redirecting across reuseport groups is not allowed. */
static void test_reuseport_mixed_groups(int family, int sotype, int sock_map,
					int verd_map, int reuseport_prog)
{
	struct sockaddr_storage addr;
	int s1, s2, c, err;
	unsigned int drop;
	socklen_t len;
	u32 key;

	zero_verdict_count(verd_map);

	/* Create two listeners, each in its own reuseport group */
	s1 = socket_loopback_reuseport(family, sotype, reuseport_prog);
	if (s1 < 0)
		return;

	s2 = socket_loopback_reuseport(family, sotype, reuseport_prog);
	if (s2 < 0)
		goto close_srv1;

	err = add_to_sockmap(sock_map, s1, s2);
	if (err)
		goto close_srv2;

	/* Connect to s2, reuseport BPF selects s1 via sock_map[0] */
	len = sizeof(addr);
	err = xgetsockname(s2, sockaddr(&addr), &len);
	if (err)
		goto close_srv2;

	c = xsocket(family, sotype, 0);
	if (c < 0)
		goto close_srv2;

	err = connect(c, sockaddr(&addr), len);
	if (sotype == SOCK_DGRAM) {
		char b = 'a';
		ssize_t n;

		n = xsend(c, &b, sizeof(b), 0);
		if (n == -1)
			goto close_cli;

		n = recv_timeout(c, &b, sizeof(b), 0, IO_TIMEOUT_SEC);
		err = n == -1;
	}
	if (!err || errno != ECONNREFUSED) {
		FAIL_ERRNO("connect: expected ECONNREFUSED");
		goto close_cli;
	}

	/* Expect drop, can't redirect outside of reuseport group */
	key = SK_DROP;
	err = xbpf_map_lookup_elem(verd_map, &key, &drop);
	if (err)
		goto close_cli;
	if (drop != 1)
		FAIL("want drop count 1, have %d", drop);

close_cli:
	xclose(c);
close_srv2:
	xclose(s2);
close_srv1:
	xclose(s1);
}

#define TEST(fn, ...)                                                          \
	{                                                                      \
		fn, #fn, __VA_ARGS__                                           \
	}

static void test_ops_cleanup(const struct bpf_map *map)
{
	const struct bpf_map_def *def;
	int err, mapfd;
	u32 key;

	def = bpf_map__def(map);
	mapfd = bpf_map__fd(map);

	for (key = 0; key < def->max_entries; key++) {
		err = bpf_map_delete_elem(mapfd, &key);
		if (err && errno != EINVAL && errno != ENOENT)
			FAIL_ERRNO("map_delete: expected EINVAL/ENOENT");
	}
}

static const char *family_str(sa_family_t family)
{
	switch (family) {
	case AF_INET:
		return "IPv4";
	case AF_INET6:
		return "IPv6";
	case AF_UNIX:
		return "Unix";
	default:
		return "unknown";
	}
}

static const char *map_type_str(const struct bpf_map *map)
{
	const struct bpf_map_def *def;

	def = bpf_map__def(map);
	if (IS_ERR(def))
		return "invalid";

	switch (def->type) {
	case BPF_MAP_TYPE_SOCKMAP:
		return "sockmap";
	case BPF_MAP_TYPE_SOCKHASH:
		return "sockhash";
	default:
		return "unknown";
	}
}

static const char *sotype_str(int sotype)
{
	switch (sotype) {
	case SOCK_DGRAM:
		return "UDP";
	case SOCK_STREAM:
		return "TCP";
	default:
		return "unknown";
	}
}

static void test_ops(struct test_sockmap_listen *skel, struct bpf_map *map,
		     int family, int sotype)
{
	const struct op_test {
		void (*fn)(int family, int sotype, int mapfd);
		const char *name;
		int sotype;
	} tests[] = {
		/* insert */
		TEST(test_insert_invalid),
		TEST(test_insert_opened),
		TEST(test_insert_bound, SOCK_STREAM),
		TEST(test_insert),
		/* delete */
		TEST(test_delete_after_insert),
		TEST(test_delete_after_close),
		/* lookup */
		TEST(test_lookup_after_insert),
		TEST(test_lookup_after_delete),
		TEST(test_lookup_32_bit_value),
		/* update */
		TEST(test_update_existing),
		/* races with insert/delete */
		TEST(test_destroy_orphan_child, SOCK_STREAM),
		TEST(test_syn_recv_insert_delete, SOCK_STREAM),
		TEST(test_race_insert_listen, SOCK_STREAM),
		/* child clone */
		TEST(test_clone_after_delete, SOCK_STREAM),
		TEST(test_accept_after_delete, SOCK_STREAM),
		TEST(test_accept_before_delete, SOCK_STREAM),
	};
	const char *family_name, *map_name, *sotype_name;
	const struct op_test *t;
	char s[MAX_TEST_NAME];
	int map_fd;

	family_name = family_str(family);
	map_name = map_type_str(map);
	sotype_name = sotype_str(sotype);
	map_fd = bpf_map__fd(map);

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		snprintf(s, sizeof(s), "%s %s %s %s", map_name, family_name,
			 sotype_name, t->name);

		if (t->sotype != 0 && t->sotype != sotype)
			continue;

		if (!test__start_subtest(s))
			continue;

		t->fn(family, sotype, map_fd);
		test_ops_cleanup(map);
	}
}

static void test_redir(struct test_sockmap_listen *skel, struct bpf_map *map,
		       int family, int sotype)
{
	const struct redir_test {
		void (*fn)(struct test_sockmap_listen *skel,
			   struct bpf_map *map, int family, int sotype);
		const char *name;
	} tests[] = {
		TEST(test_skb_redir_to_connected),
		TEST(test_skb_redir_to_listening),
		TEST(test_msg_redir_to_connected),
		TEST(test_msg_redir_to_listening),
	};
	const char *family_name, *map_name;
	const struct redir_test *t;
	char s[MAX_TEST_NAME];

	family_name = family_str(family);
	map_name = map_type_str(map);

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		snprintf(s, sizeof(s), "%s %s %s", map_name, family_name,
			 t->name);

		if (!test__start_subtest(s))
			continue;

		t->fn(skel, map, family, sotype);
	}
}

static void unix_redir_to_connected(int sotype, int sock_mapfd,
			       int verd_mapfd, enum redir_mode mode)
{
	const char *log_prefix = redir_mode_str(mode);
	int c0, c1, p0, p1;
	unsigned int pass;
	int retries = 100;
	int err, n;
	int sfd[2];
	u32 key;
	char b;

	zero_verdict_count(verd_mapfd);

	if (socketpair(AF_UNIX, sotype | SOCK_NONBLOCK, 0, sfd))
		return;
	c0 = sfd[0], p0 = sfd[1];

	if (socketpair(AF_UNIX, sotype | SOCK_NONBLOCK, 0, sfd))
		goto close0;
	c1 = sfd[0], p1 = sfd[1];

	err = add_to_sockmap(sock_mapfd, p0, p1);
	if (err)
		goto close;

	n = write(c1, "a", 1);
	if (n < 0)
		FAIL_ERRNO("%s: write", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete write", log_prefix);
	if (n < 1)
		goto close;

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &pass);
	if (err)
		goto close;
	if (pass != 1)
		FAIL("%s: want pass count 1, have %d", log_prefix, pass);

again:
	n = read(mode == REDIR_INGRESS ? p0 : c0, &b, 1);
	if (n < 0) {
		if (errno == EAGAIN && retries--)
			goto again;
		FAIL_ERRNO("%s: read", log_prefix);
	}
	if (n == 0)
		FAIL("%s: incomplete read", log_prefix);

close:
	xclose(c1);
	xclose(p1);
close0:
	xclose(c0);
	xclose(p0);
}

static void unix_skb_redir_to_connected(struct test_sockmap_listen *skel,
					struct bpf_map *inner_map, int sotype)
{
	int verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_VERDICT, 0);
	if (err)
		return;

	skel->bss->test_ingress = false;
	unix_redir_to_connected(sotype, sock_map, verdict_map, REDIR_EGRESS);
	skel->bss->test_ingress = true;
	unix_redir_to_connected(sotype, sock_map, verdict_map, REDIR_INGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_VERDICT);
}

static void test_unix_redir(struct test_sockmap_listen *skel, struct bpf_map *map,
			    int sotype)
{
	const char *family_name, *map_name;
	char s[MAX_TEST_NAME];

	family_name = family_str(AF_UNIX);
	map_name = map_type_str(map);
	snprintf(s, sizeof(s), "%s %s %s", map_name, family_name, __func__);
	if (!test__start_subtest(s))
		return;
	unix_skb_redir_to_connected(skel, map, sotype);
}

static void test_reuseport(struct test_sockmap_listen *skel,
			   struct bpf_map *map, int family, int sotype)
{
	const struct reuseport_test {
		void (*fn)(int family, int sotype, int socket_map,
			   int verdict_map, int reuseport_prog);
		const char *name;
		int sotype;
	} tests[] = {
		TEST(test_reuseport_select_listening),
		TEST(test_reuseport_select_connected),
		TEST(test_reuseport_mixed_groups),
	};
	int socket_map, verdict_map, reuseport_prog;
	const char *family_name, *map_name, *sotype_name;
	const struct reuseport_test *t;
	char s[MAX_TEST_NAME];

	family_name = family_str(family);
	map_name = map_type_str(map);
	sotype_name = sotype_str(sotype);

	socket_map = bpf_map__fd(map);
	verdict_map = bpf_map__fd(skel->maps.verdict_map);
	reuseport_prog = bpf_program__fd(skel->progs.prog_reuseport);

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		snprintf(s, sizeof(s), "%s %s %s %s", map_name, family_name,
			 sotype_name, t->name);

		if (t->sotype != 0 && t->sotype != sotype)
			continue;

		if (!test__start_subtest(s))
			continue;

		t->fn(family, sotype, socket_map, verdict_map, reuseport_prog);
	}
}

static int udp_socketpair(int family, int *s, int *c)
{
	struct sockaddr_storage addr;
	socklen_t len;
	int p0, c0;
	int err;

	p0 = socket_loopback(family, SOCK_DGRAM | SOCK_NONBLOCK);
	if (p0 < 0)
		return p0;

	len = sizeof(addr);
	err = xgetsockname(p0, sockaddr(&addr), &len);
	if (err)
		goto close_peer0;

	c0 = xsocket(family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (c0 < 0) {
		err = c0;
		goto close_peer0;
	}
	err = xconnect(c0, sockaddr(&addr), len);
	if (err)
		goto close_cli0;
	err = xgetsockname(c0, sockaddr(&addr), &len);
	if (err)
		goto close_cli0;
	err = xconnect(p0, sockaddr(&addr), len);
	if (err)
		goto close_cli0;

	*s = p0;
	*c = c0;
	return 0;

close_cli0:
	xclose(c0);
close_peer0:
	xclose(p0);
	return err;
}

static void udp_redir_to_connected(int family, int sock_mapfd, int verd_mapfd,
				   enum redir_mode mode)
{
	const char *log_prefix = redir_mode_str(mode);
	int c0, c1, p0, p1;
	unsigned int pass;
	int retries = 100;
	int err, n;
	u32 key;
	char b;

	zero_verdict_count(verd_mapfd);

	err = udp_socketpair(family, &p0, &c0);
	if (err)
		return;
	err = udp_socketpair(family, &p1, &c1);
	if (err)
		goto close_cli0;

	err = add_to_sockmap(sock_mapfd, p0, p1);
	if (err)
		goto close_cli1;

	n = write(c1, "a", 1);
	if (n < 0)
		FAIL_ERRNO("%s: write", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete write", log_prefix);
	if (n < 1)
		goto close_cli1;

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &pass);
	if (err)
		goto close_cli1;
	if (pass != 1)
		FAIL("%s: want pass count 1, have %d", log_prefix, pass);

again:
	n = read(mode == REDIR_INGRESS ? p0 : c0, &b, 1);
	if (n < 0) {
		if (errno == EAGAIN && retries--)
			goto again;
		FAIL_ERRNO("%s: read", log_prefix);
	}
	if (n == 0)
		FAIL("%s: incomplete read", log_prefix);

close_cli1:
	xclose(c1);
	xclose(p1);
close_cli0:
	xclose(c0);
	xclose(p0);
}

static void udp_skb_redir_to_connected(struct test_sockmap_listen *skel,
				       struct bpf_map *inner_map, int family)
{
	int verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_VERDICT, 0);
	if (err)
		return;

	skel->bss->test_ingress = false;
	udp_redir_to_connected(family, sock_map, verdict_map, REDIR_EGRESS);
	skel->bss->test_ingress = true;
	udp_redir_to_connected(family, sock_map, verdict_map, REDIR_INGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_VERDICT);
}

static void test_udp_redir(struct test_sockmap_listen *skel, struct bpf_map *map,
			   int family)
{
	const char *family_name, *map_name;
	char s[MAX_TEST_NAME];

	family_name = family_str(family);
	map_name = map_type_str(map);
	snprintf(s, sizeof(s), "%s %s %s", map_name, family_name, __func__);
	if (!test__start_subtest(s))
		return;
	udp_skb_redir_to_connected(skel, map, family);
}

static void udp_unix_redir_to_connected(int family, int sock_mapfd,
					int verd_mapfd, enum redir_mode mode)
{
	const char *log_prefix = redir_mode_str(mode);
	int c0, c1, p0, p1;
	unsigned int pass;
	int retries = 100;
	int err, n;
	int sfd[2];
	u32 key;
	char b;

	zero_verdict_count(verd_mapfd);

	if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, sfd))
		return;
	c0 = sfd[0], p0 = sfd[1];

	err = udp_socketpair(family, &p1, &c1);
	if (err)
		goto close;

	err = add_to_sockmap(sock_mapfd, p0, p1);
	if (err)
		goto close_cli1;

	n = write(c1, "a", 1);
	if (n < 0)
		FAIL_ERRNO("%s: write", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete write", log_prefix);
	if (n < 1)
		goto close_cli1;

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &pass);
	if (err)
		goto close_cli1;
	if (pass != 1)
		FAIL("%s: want pass count 1, have %d", log_prefix, pass);

again:
	n = read(mode == REDIR_INGRESS ? p0 : c0, &b, 1);
	if (n < 0) {
		if (errno == EAGAIN && retries--)
			goto again;
		FAIL_ERRNO("%s: read", log_prefix);
	}
	if (n == 0)
		FAIL("%s: incomplete read", log_prefix);

close_cli1:
	xclose(c1);
	xclose(p1);
close:
	xclose(c0);
	xclose(p0);
}

static void udp_unix_skb_redir_to_connected(struct test_sockmap_listen *skel,
					    struct bpf_map *inner_map, int family)
{
	int verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_VERDICT, 0);
	if (err)
		return;

	skel->bss->test_ingress = false;
	udp_unix_redir_to_connected(family, sock_map, verdict_map, REDIR_EGRESS);
	skel->bss->test_ingress = true;
	udp_unix_redir_to_connected(family, sock_map, verdict_map, REDIR_INGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_VERDICT);
}

static void unix_udp_redir_to_connected(int family, int sock_mapfd,
					int verd_mapfd, enum redir_mode mode)
{
	const char *log_prefix = redir_mode_str(mode);
	int c0, c1, p0, p1;
	unsigned int pass;
	int err, n;
	int sfd[2];
	u32 key;
	char b;

	zero_verdict_count(verd_mapfd);

	err = udp_socketpair(family, &p0, &c0);
	if (err)
		return;

	if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, sfd))
		goto close_cli0;
	c1 = sfd[0], p1 = sfd[1];

	err = add_to_sockmap(sock_mapfd, p0, p1);
	if (err)
		goto close;

	n = write(c1, "a", 1);
	if (n < 0)
		FAIL_ERRNO("%s: write", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete write", log_prefix);
	if (n < 1)
		goto close;

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &pass);
	if (err)
		goto close;
	if (pass != 1)
		FAIL("%s: want pass count 1, have %d", log_prefix, pass);

	n = read(mode == REDIR_INGRESS ? p0 : c0, &b, 1);
	if (n < 0)
		FAIL_ERRNO("%s: read", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete read", log_prefix);

close:
	xclose(c1);
	xclose(p1);
close_cli0:
	xclose(c0);
	xclose(p0);

}

static void unix_udp_skb_redir_to_connected(struct test_sockmap_listen *skel,
					    struct bpf_map *inner_map, int family)
{
	int verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_VERDICT, 0);
	if (err)
		return;

	skel->bss->test_ingress = false;
	unix_udp_redir_to_connected(family, sock_map, verdict_map, REDIR_EGRESS);
	skel->bss->test_ingress = true;
	unix_udp_redir_to_connected(family, sock_map, verdict_map, REDIR_INGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_VERDICT);
}

static void test_udp_unix_redir(struct test_sockmap_listen *skel, struct bpf_map *map,
				int family)
{
	const char *family_name, *map_name;
	char s[MAX_TEST_NAME];

	family_name = family_str(family);
	map_name = map_type_str(map);
	snprintf(s, sizeof(s), "%s %s %s", map_name, family_name, __func__);
	if (!test__start_subtest(s))
		return;
	udp_unix_skb_redir_to_connected(skel, map, family);
	unix_udp_skb_redir_to_connected(skel, map, family);
}

static void run_tests(struct test_sockmap_listen *skel, struct bpf_map *map,
		      int family)
{
	test_ops(skel, map, family, SOCK_STREAM);
	test_ops(skel, map, family, SOCK_DGRAM);
	test_redir(skel, map, family, SOCK_STREAM);
	test_reuseport(skel, map, family, SOCK_STREAM);
	test_reuseport(skel, map, family, SOCK_DGRAM);
	test_udp_redir(skel, map, family);
	test_udp_unix_redir(skel, map, family);
}

void test_sockmap_listen(void)
{
	struct test_sockmap_listen *skel;

	skel = test_sockmap_listen__open_and_load();
	if (!skel) {
		FAIL("skeleton open/load failed");
		return;
	}

	skel->bss->test_sockmap = true;
	run_tests(skel, skel->maps.sock_map, AF_INET);
	run_tests(skel, skel->maps.sock_map, AF_INET6);
	test_unix_redir(skel, skel->maps.sock_map, SOCK_DGRAM);

	skel->bss->test_sockmap = false;
	run_tests(skel, skel->maps.sock_hash, AF_INET);
	run_tests(skel, skel->maps.sock_hash, AF_INET6);
	test_unix_redir(skel, skel->maps.sock_hash, SOCK_DGRAM);

	test_sockmap_listen__destroy(skel);
}
