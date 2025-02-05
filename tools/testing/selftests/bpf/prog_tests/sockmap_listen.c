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
#include <linux/vm_sockets.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_util.h"
#include "test_progs.h"
#include "test_sockmap_listen.skel.h"

#include "sockmap_helpers.h"

#define NO_FLAGS 0

static void test_insert_invalid(struct test_sockmap_listen *skel __always_unused,
				int family, int sotype, int mapfd)
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

static void test_insert_opened(struct test_sockmap_listen *skel __always_unused,
			       int family, int sotype, int mapfd)
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

static void test_insert_bound(struct test_sockmap_listen *skel __always_unused,
			      int family, int sotype, int mapfd)
{
	struct sockaddr_storage addr;
	socklen_t len = 0;
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

static void test_insert(struct test_sockmap_listen *skel __always_unused,
			int family, int sotype, int mapfd)
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

static void test_delete_after_insert(struct test_sockmap_listen *skel __always_unused,
				     int family, int sotype, int mapfd)
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

static void test_delete_after_close(struct test_sockmap_listen *skel __always_unused,
				    int family, int sotype, int mapfd)
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

static void test_lookup_after_insert(struct test_sockmap_listen *skel __always_unused,
				     int family, int sotype, int mapfd)
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

static void test_lookup_after_delete(struct test_sockmap_listen *skel __always_unused,
				     int family, int sotype, int mapfd)
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

static void test_lookup_32_bit_value(struct test_sockmap_listen *skel __always_unused,
				     int family, int sotype, int mapfd)
{
	u32 key, value32;
	int err, s;

	s = socket_loopback(family, sotype);
	if (s < 0)
		return;

	mapfd = bpf_map_create(BPF_MAP_TYPE_SOCKMAP, NULL, sizeof(key),
			       sizeof(value32), 1, NULL);
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

static void test_update_existing(struct test_sockmap_listen *skel __always_unused,
				 int family, int sotype, int mapfd)
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
static void do_destroy_orphan_child(int family, int sotype, int mapfd)
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

static void test_destroy_orphan_child(struct test_sockmap_listen *skel,
				      int family, int sotype, int mapfd)
{
	int msg_verdict = bpf_program__fd(skel->progs.prog_msg_verdict);
	int skb_verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	const struct test {
		int progfd;
		enum bpf_attach_type atype;
	} tests[] = {
		{ -1, -1 },
		{ msg_verdict, BPF_SK_MSG_VERDICT },
		{ skb_verdict, BPF_SK_SKB_VERDICT },
	};
	const struct test *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		if (t->progfd != -1 &&
		    xbpf_prog_attach(t->progfd, mapfd, t->atype, 0) != 0)
			return;

		do_destroy_orphan_child(family, sotype, mapfd);

		if (t->progfd != -1)
			xbpf_prog_detach2(t->progfd, mapfd, t->atype);
	}
}

/* Perform a passive open after removing listening socket from SOCKMAP
 * to ensure that callbacks get restored properly.
 */
static void test_clone_after_delete(struct test_sockmap_listen *skel __always_unused,
				    int family, int sotype, int mapfd)
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
static void test_accept_after_delete(struct test_sockmap_listen *skel __always_unused,
				     int family, int sotype, int mapfd)
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
static void test_accept_before_delete(struct test_sockmap_listen *skel __always_unused,
				      int family, int sotype, int mapfd)
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

static void test_syn_recv_insert_delete(struct test_sockmap_listen *skel __always_unused,
					int family, int sotype, int mapfd)
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

static void test_race_insert_listen(struct test_sockmap_listen *skel __always_unused,
				    int family, int socktype, int mapfd)
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

static void redir_to_connected(int family, int sotype, int sock_mapfd,
			       int verd_mapfd, enum redir_mode mode)
{
	const char *log_prefix = redir_mode_str(mode);
	int c0, c1, p0, p1;
	unsigned int pass;
	int err, n;
	u32 key;
	char b;

	zero_verdict_count(verd_mapfd);

	err = create_socket_pairs(family, sotype | SOCK_NONBLOCK, &c0, &c1,
				  &p0, &p1);
	if (err)
		return;

	err = add_to_sockmap(sock_mapfd, p0, p1);
	if (err)
		goto close;

	n = write(mode == REDIR_INGRESS ? c1 : p1, "a", 1);
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
	n = recv_timeout(c0, &b, 1, 0, IO_TIMEOUT_SEC);
	if (n < 0)
		FAIL_ERRNO("%s: recv_timeout", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete recv", log_prefix);

close:
	xclose(p1);
	xclose(c1);
	xclose(p0);
	xclose(c0);
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

static void test_msg_redir_to_connected_with_link(struct test_sockmap_listen *skel,
						  struct bpf_map *inner_map, int family,
						  int sotype)
{
	int prog_msg_verdict = bpf_program__fd(skel->progs.prog_msg_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int link_fd;

	link_fd = bpf_link_create(prog_msg_verdict, sock_map, BPF_SK_MSG_VERDICT, NULL);
	if (!ASSERT_GE(link_fd, 0, "bpf_link_create"))
		return;

	redir_to_connected(family, sotype, sock_map, verdict_map, REDIR_EGRESS);

	close(link_fd);
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

static void test_msg_redir_to_listening_with_link(struct test_sockmap_listen *skel,
						  struct bpf_map *inner_map, int family,
						  int sotype)
{
	struct bpf_program *verdict = skel->progs.prog_msg_verdict;
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	struct bpf_link *link;

	link = bpf_program__attach_sockmap(verdict, sock_map);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_sockmap"))
		return;

	redir_to_listening(family, sotype, sock_map, verdict_map, REDIR_EGRESS);

	bpf_link__detach(link);
}

static void redir_partial(int family, int sotype, int sock_map, int parser_map)
{
	int c0 = -1, c1 = -1, p0 = -1, p1 = -1;
	int err, n, key, value;
	char buf[] = "abc";

	key = 0;
	value = sizeof(buf) - 1;
	err = xbpf_map_update_elem(parser_map, &key, &value, 0);
	if (err)
		return;

	err = create_socket_pairs(family, sotype | SOCK_NONBLOCK, &c0, &c1,
				  &p0, &p1);
	if (err)
		goto clean_parser_map;

	err = add_to_sockmap(sock_map, p0, p1);
	if (err)
		goto close;

	n = xsend(c1, buf, sizeof(buf), 0);
	if (n < sizeof(buf))
		FAIL("incomplete write");

	n = xrecv_nonblock(c0, buf, sizeof(buf), 0);
	if (n != sizeof(buf) - 1)
		FAIL("expect %zu, received %d", sizeof(buf) - 1, n);

close:
	xclose(c0);
	xclose(p0);
	xclose(c1);
	xclose(p1);

clean_parser_map:
	key = 0;
	value = 0;
	xbpf_map_update_elem(parser_map, &key, &value, 0);
}

static void test_skb_redir_partial(struct test_sockmap_listen *skel,
				   struct bpf_map *inner_map, int family,
				   int sotype)
{
	int verdict = bpf_program__fd(skel->progs.prog_stream_verdict);
	int parser = bpf_program__fd(skel->progs.prog_stream_parser);
	int parser_map = bpf_map__fd(skel->maps.parser_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(parser, sock_map, BPF_SK_SKB_STREAM_PARSER, 0);
	if (err)
		return;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err)
		goto detach;

	redir_partial(family, sotype, sock_map, parser_map);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_STREAM_VERDICT);
detach:
	xbpf_prog_detach2(parser, sock_map, BPF_SK_SKB_STREAM_PARSER);
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
	int err, mapfd;
	u32 key;

	mapfd = bpf_map__fd(map);

	for (key = 0; key < bpf_map__max_entries(map); key++) {
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
	case AF_VSOCK:
		return "VSOCK";
	default:
		return "unknown";
	}
}

static const char *map_type_str(const struct bpf_map *map)
{
	int type;

	if (!map)
		return "invalid";
	type = bpf_map__type(map);

	switch (type) {
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
		void (*fn)(struct test_sockmap_listen *skel,
			   int family, int sotype, int mapfd);
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

		t->fn(skel, family, sotype, map_fd);
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
		TEST(test_skb_redir_partial),
		TEST(test_msg_redir_to_connected),
		TEST(test_msg_redir_to_connected_with_link),
		TEST(test_msg_redir_to_listening),
		TEST(test_msg_redir_to_listening_with_link),
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

static void pairs_redir_to_connected(int cli0, int peer0, int cli1, int peer1,
				     int sock_mapfd, int nop_mapfd,
				     int verd_mapfd, enum redir_mode mode,
				     int send_flags)
{
	const char *log_prefix = redir_mode_str(mode);
	unsigned int pass;
	int err, n;
	u32 key;
	char b;

	zero_verdict_count(verd_mapfd);

	err = add_to_sockmap(sock_mapfd, peer0, peer1);
	if (err)
		return;

	if (nop_mapfd >= 0) {
		err = add_to_sockmap(nop_mapfd, cli0, cli1);
		if (err)
			return;
	}

	/* Last byte is OOB data when send_flags has MSG_OOB bit set */
	n = xsend(cli1, "ab", 2, send_flags);
	if (n >= 0 && n < 2)
		FAIL("%s: incomplete send", log_prefix);
	if (n < 2)
		return;

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &pass);
	if (err)
		return;
	if (pass != 1)
		FAIL("%s: want pass count 1, have %d", log_prefix, pass);

	n = recv_timeout(mode == REDIR_INGRESS ? peer0 : cli0, &b, 1, 0, IO_TIMEOUT_SEC);
	if (n < 0)
		FAIL_ERRNO("%s: recv_timeout", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete recv", log_prefix);

	if (send_flags & MSG_OOB) {
		/* Check that we can't read OOB while in sockmap */
		errno = 0;
		n = recv(peer1, &b, 1, MSG_OOB | MSG_DONTWAIT);
		if (n != -1 || errno != EOPNOTSUPP)
			FAIL("%s: recv(MSG_OOB): expected EOPNOTSUPP: retval=%d errno=%d",
			     log_prefix, n, errno);

		/* Remove peer1 from sockmap */
		xbpf_map_delete_elem(sock_mapfd, &(int){ 1 });

		/* Check that OOB was dropped on redirect */
		errno = 0;
		n = recv(peer1, &b, 1, MSG_OOB | MSG_DONTWAIT);
		if (n != -1 || errno != EINVAL)
			FAIL("%s: recv(MSG_OOB): expected EINVAL: retval=%d errno=%d",
			     log_prefix, n, errno);
	}
}

static void unix_redir_to_connected(int sotype, int sock_mapfd,
			       int verd_mapfd, enum redir_mode mode)
{
	int c0, c1, p0, p1;
	int sfd[2];

	if (socketpair(AF_UNIX, sotype | SOCK_NONBLOCK, 0, sfd))
		return;
	c0 = sfd[0], p0 = sfd[1];

	if (socketpair(AF_UNIX, sotype | SOCK_NONBLOCK, 0, sfd))
		goto close0;
	c1 = sfd[0], p1 = sfd[1];

	pairs_redir_to_connected(c0, p0, c1, p1, sock_mapfd, -1, verd_mapfd,
				 mode, NO_FLAGS);

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

/* Returns two connected loopback vsock sockets */
static int vsock_socketpair_connectible(int sotype, int *v0, int *v1)
{
	return create_pair(AF_VSOCK, sotype | SOCK_NONBLOCK, v0, v1);
}

static void vsock_unix_redir_connectible(int sock_mapfd, int verd_mapfd,
					 enum redir_mode mode, int sotype)
{
	const char *log_prefix = redir_mode_str(mode);
	char a = 'a', b = 'b';
	int u0, u1, v0, v1;
	int sfd[2];
	unsigned int pass;
	int err, n;
	u32 key;

	zero_verdict_count(verd_mapfd);

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sfd))
		return;

	u0 = sfd[0];
	u1 = sfd[1];

	err = vsock_socketpair_connectible(sotype, &v0, &v1);
	if (err) {
		FAIL("vsock_socketpair_connectible() failed");
		goto close_uds;
	}

	err = add_to_sockmap(sock_mapfd, u0, v0);
	if (err) {
		FAIL("add_to_sockmap failed");
		goto close_vsock;
	}

	n = write(v1, &a, sizeof(a));
	if (n < 0)
		FAIL_ERRNO("%s: write", log_prefix);
	if (n == 0)
		FAIL("%s: incomplete write", log_prefix);
	if (n < 1)
		goto out;

	n = xrecv_nonblock(mode == REDIR_INGRESS ? u0 : u1, &b, sizeof(b), 0);
	if (n < 0)
		FAIL("%s: recv() err, errno=%d", log_prefix, errno);
	if (n == 0)
		FAIL("%s: incomplete recv", log_prefix);
	if (b != a)
		FAIL("%s: vsock socket map failed, %c != %c", log_prefix, a, b);

	key = SK_PASS;
	err = xbpf_map_lookup_elem(verd_mapfd, &key, &pass);
	if (err)
		goto out;
	if (pass != 1)
		FAIL("%s: want pass count 1, have %d", log_prefix, pass);
out:
	key = 0;
	bpf_map_delete_elem(sock_mapfd, &key);
	key = 1;
	bpf_map_delete_elem(sock_mapfd, &key);

close_vsock:
	close(v0);
	close(v1);

close_uds:
	close(u0);
	close(u1);
}

static void vsock_unix_skb_redir_connectible(struct test_sockmap_listen *skel,
					     struct bpf_map *inner_map,
					     int sotype)
{
	int verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_VERDICT, 0);
	if (err)
		return;

	skel->bss->test_ingress = false;
	vsock_unix_redir_connectible(sock_map, verdict_map, REDIR_EGRESS, sotype);
	skel->bss->test_ingress = true;
	vsock_unix_redir_connectible(sock_map, verdict_map, REDIR_INGRESS, sotype);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_VERDICT);
}

static void test_vsock_redir(struct test_sockmap_listen *skel, struct bpf_map *map)
{
	const char *family_name, *map_name;
	char s[MAX_TEST_NAME];

	family_name = family_str(AF_VSOCK);
	map_name = map_type_str(map);
	snprintf(s, sizeof(s), "%s %s %s", map_name, family_name, __func__);
	if (!test__start_subtest(s))
		return;

	vsock_unix_skb_redir_connectible(skel, map, SOCK_STREAM);
	vsock_unix_skb_redir_connectible(skel, map, SOCK_SEQPACKET);
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

static int inet_socketpair(int family, int type, int *s, int *c)
{
	return create_pair(family, type | SOCK_NONBLOCK, s, c);
}

static void udp_redir_to_connected(int family, int sock_mapfd, int verd_mapfd,
				   enum redir_mode mode)
{
	int c0, c1, p0, p1;
	int err;

	err = inet_socketpair(family, SOCK_DGRAM, &p0, &c0);
	if (err)
		return;
	err = inet_socketpair(family, SOCK_DGRAM, &p1, &c1);
	if (err)
		goto close_cli0;

	pairs_redir_to_connected(c0, p0, c1, p1, sock_mapfd, -1, verd_mapfd,
				 mode, NO_FLAGS);

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

static void inet_unix_redir_to_connected(int family, int type, int sock_mapfd,
					int verd_mapfd, enum redir_mode mode)
{
	int c0, c1, p0, p1;
	int sfd[2];
	int err;

	if (socketpair(AF_UNIX, type | SOCK_NONBLOCK, 0, sfd))
		return;
	c0 = sfd[0], p0 = sfd[1];

	err = inet_socketpair(family, type, &p1, &c1);
	if (err)
		goto close;

	pairs_redir_to_connected(c0, p0, c1, p1, sock_mapfd, -1, verd_mapfd,
				 mode, NO_FLAGS);

	xclose(c1);
	xclose(p1);
close:
	xclose(c0);
	xclose(p0);
}

static void inet_unix_skb_redir_to_connected(struct test_sockmap_listen *skel,
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
	inet_unix_redir_to_connected(family, SOCK_DGRAM, sock_map, verdict_map,
				    REDIR_EGRESS);
	inet_unix_redir_to_connected(family, SOCK_STREAM, sock_map, verdict_map,
				    REDIR_EGRESS);
	skel->bss->test_ingress = true;
	inet_unix_redir_to_connected(family, SOCK_DGRAM, sock_map, verdict_map,
				    REDIR_INGRESS);
	inet_unix_redir_to_connected(family, SOCK_STREAM, sock_map, verdict_map,
				    REDIR_INGRESS);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_VERDICT);
}

static void unix_inet_redir_to_connected(int family, int type, int sock_mapfd,
					 int nop_mapfd, int verd_mapfd,
					 enum redir_mode mode, int send_flags)
{
	int c0, c1, p0, p1;
	int sfd[2];
	int err;

	err = inet_socketpair(family, type, &p0, &c0);
	if (err)
		return;

	if (socketpair(AF_UNIX, type | SOCK_NONBLOCK, 0, sfd))
		goto close_cli0;
	c1 = sfd[0], p1 = sfd[1];

	pairs_redir_to_connected(c0, p0, c1, p1, sock_mapfd, nop_mapfd,
				 verd_mapfd, mode, send_flags);

	xclose(c1);
	xclose(p1);
close_cli0:
	xclose(c0);
	xclose(p0);
}

static void unix_inet_skb_redir_to_connected(struct test_sockmap_listen *skel,
					    struct bpf_map *inner_map, int family)
{
	int verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	int nop_map = bpf_map__fd(skel->maps.nop_map);
	int verdict_map = bpf_map__fd(skel->maps.verdict_map);
	int sock_map = bpf_map__fd(inner_map);
	int err;

	err = xbpf_prog_attach(verdict, sock_map, BPF_SK_SKB_VERDICT, 0);
	if (err)
		return;

	skel->bss->test_ingress = false;
	unix_inet_redir_to_connected(family, SOCK_DGRAM,
				     sock_map, -1, verdict_map,
				     REDIR_EGRESS, NO_FLAGS);
	unix_inet_redir_to_connected(family, SOCK_STREAM,
				     sock_map, -1, verdict_map,
				     REDIR_EGRESS, NO_FLAGS);

	unix_inet_redir_to_connected(family, SOCK_DGRAM,
				     sock_map, nop_map, verdict_map,
				     REDIR_EGRESS, NO_FLAGS);
	unix_inet_redir_to_connected(family, SOCK_STREAM,
				     sock_map, nop_map, verdict_map,
				     REDIR_EGRESS, NO_FLAGS);

	/* MSG_OOB not supported by AF_UNIX SOCK_DGRAM */
	unix_inet_redir_to_connected(family, SOCK_STREAM,
				     sock_map, nop_map, verdict_map,
				     REDIR_EGRESS, MSG_OOB);

	skel->bss->test_ingress = true;
	unix_inet_redir_to_connected(family, SOCK_DGRAM,
				     sock_map, -1, verdict_map,
				     REDIR_INGRESS, NO_FLAGS);
	unix_inet_redir_to_connected(family, SOCK_STREAM,
				     sock_map, -1, verdict_map,
				     REDIR_INGRESS, NO_FLAGS);

	unix_inet_redir_to_connected(family, SOCK_DGRAM,
				     sock_map, nop_map, verdict_map,
				     REDIR_INGRESS, NO_FLAGS);
	unix_inet_redir_to_connected(family, SOCK_STREAM,
				     sock_map, nop_map, verdict_map,
				     REDIR_INGRESS, NO_FLAGS);

	/* MSG_OOB not supported by AF_UNIX SOCK_DGRAM */
	unix_inet_redir_to_connected(family, SOCK_STREAM,
				     sock_map, nop_map, verdict_map,
				     REDIR_INGRESS, MSG_OOB);

	xbpf_prog_detach2(verdict, sock_map, BPF_SK_SKB_VERDICT);
}

static void test_udp_unix_redir(struct test_sockmap_listen *skel, struct bpf_map *map,
				int family)
{
	const char *family_name, *map_name;
	struct netns_obj *netns;
	char s[MAX_TEST_NAME];

	family_name = family_str(family);
	map_name = map_type_str(map);
	snprintf(s, sizeof(s), "%s %s %s", map_name, family_name, __func__);
	if (!test__start_subtest(s))
		return;

	netns = netns_new("sockmap_listen", true);
	if (!ASSERT_OK_PTR(netns, "netns_new"))
		return;

	inet_unix_skb_redir_to_connected(skel, map, family);
	unix_inet_skb_redir_to_connected(skel, map, family);

	netns_free(netns);
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

void serial_test_sockmap_listen(void)
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
	test_unix_redir(skel, skel->maps.sock_map, SOCK_STREAM);
	test_vsock_redir(skel, skel->maps.sock_map);

	skel->bss->test_sockmap = false;
	run_tests(skel, skel->maps.sock_hash, AF_INET);
	run_tests(skel, skel->maps.sock_hash, AF_INET6);
	test_unix_redir(skel, skel->maps.sock_hash, SOCK_DGRAM);
	test_unix_redir(skel, skel->maps.sock_hash, SOCK_STREAM);
	test_vsock_redir(skel, skel->maps.sock_hash);

	test_sockmap_listen__destroy(skel);
}
