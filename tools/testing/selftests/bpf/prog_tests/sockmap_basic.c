// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
#include <error.h>
#include <netinet/tcp.h>

#include "test_progs.h"
#include "test_skmsg_load_helpers.skel.h"
#include "test_sockmap_update.skel.h"
#include "test_sockmap_invalid_update.skel.h"
#include "test_sockmap_skb_verdict_attach.skel.h"
#include "test_sockmap_progs_query.skel.h"
#include "bpf_iter_sockmap.skel.h"

#define TCP_REPAIR		19	/* TCP sock is under repair right now */

#define TCP_REPAIR_ON		1
#define TCP_REPAIR_OFF_NO_WP	-1	/* Turn off without window probes */

static int connected_socket_v4(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(80),
		.sin_addr = { inet_addr("127.0.0.1") },
	};
	socklen_t len = sizeof(addr);
	int s, repair, err;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (!ASSERT_GE(s, 0, "socket"))
		goto error;

	repair = TCP_REPAIR_ON;
	err = setsockopt(s, SOL_TCP, TCP_REPAIR, &repair, sizeof(repair));
	if (!ASSERT_OK(err, "setsockopt(TCP_REPAIR)"))
		goto error;

	err = connect(s, (struct sockaddr *)&addr, len);
	if (!ASSERT_OK(err, "connect"))
		goto error;

	repair = TCP_REPAIR_OFF_NO_WP;
	err = setsockopt(s, SOL_TCP, TCP_REPAIR, &repair, sizeof(repair));
	if (!ASSERT_OK(err, "setsockopt(TCP_REPAIR)"))
		goto error;

	return s;
error:
	perror(__func__);
	close(s);
	return -1;
}

static void compare_cookies(struct bpf_map *src, struct bpf_map *dst)
{
	__u32 i, max_entries = bpf_map__max_entries(src);
	int err, src_fd, dst_fd;

	src_fd = bpf_map__fd(src);
	dst_fd = bpf_map__fd(dst);

	for (i = 0; i < max_entries; i++) {
		__u64 src_cookie, dst_cookie;

		err = bpf_map_lookup_elem(src_fd, &i, &src_cookie);
		if (err && errno == ENOENT) {
			err = bpf_map_lookup_elem(dst_fd, &i, &dst_cookie);
			ASSERT_ERR(err, "map_lookup_elem(dst)");
			ASSERT_EQ(errno, ENOENT, "map_lookup_elem(dst)");
			continue;
		}
		if (!ASSERT_OK(err, "lookup_elem(src)"))
			continue;

		err = bpf_map_lookup_elem(dst_fd, &i, &dst_cookie);
		if (!ASSERT_OK(err, "lookup_elem(dst)"))
			continue;

		ASSERT_EQ(dst_cookie, src_cookie, "cookie mismatch");
	}
}

/* Create a map, populate it with one socket, and free the map. */
static void test_sockmap_create_update_free(enum bpf_map_type map_type)
{
	const int zero = 0;
	int s, map, err;

	s = connected_socket_v4();
	if (!ASSERT_GE(s, 0, "connected_socket_v4"))
		return;

	map = bpf_map_create(map_type, NULL, sizeof(int), sizeof(int), 1, NULL);
	if (!ASSERT_GE(map, 0, "bpf_map_create"))
		goto out;

	err = bpf_map_update_elem(map, &zero, &s, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update"))
		goto out;

out:
	close(map);
	close(s);
}

static void test_skmsg_helpers(enum bpf_map_type map_type)
{
	struct test_skmsg_load_helpers *skel;
	int err, map, verdict;

	skel = test_skmsg_load_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_skmsg_load_helpers__open_and_load"))
		return;

	verdict = bpf_program__fd(skel->progs.prog_msg_verdict);
	map = bpf_map__fd(skel->maps.sock_map);

	err = bpf_prog_attach(verdict, map, BPF_SK_MSG_VERDICT, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach"))
		goto out;

	err = bpf_prog_detach2(verdict, map, BPF_SK_MSG_VERDICT);
	if (!ASSERT_OK(err, "bpf_prog_detach2"))
		goto out;
out:
	test_skmsg_load_helpers__destroy(skel);
}

static void test_sockmap_update(enum bpf_map_type map_type)
{
	int err, prog, src;
	struct test_sockmap_update *skel;
	struct bpf_map *dst_map;
	const __u32 zero = 0;
	char dummy[14] = {0};
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = dummy,
		.data_size_in = sizeof(dummy),
		.repeat = 1,
	);
	__s64 sk;

	sk = connected_socket_v4();
	if (!ASSERT_NEQ(sk, -1, "connected_socket_v4"))
		return;

	skel = test_sockmap_update__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto close_sk;

	prog = bpf_program__fd(skel->progs.copy_sock_map);
	src = bpf_map__fd(skel->maps.src);
	if (map_type == BPF_MAP_TYPE_SOCKMAP)
		dst_map = skel->maps.dst_sock_map;
	else
		dst_map = skel->maps.dst_sock_hash;

	err = bpf_map_update_elem(src, &zero, &sk, BPF_NOEXIST);
	if (!ASSERT_OK(err, "update_elem(src)"))
		goto out;

	err = bpf_prog_test_run_opts(prog, &topts);
	if (!ASSERT_OK(err, "test_run"))
		goto out;
	if (!ASSERT_NEQ(topts.retval, 0, "test_run retval"))
		goto out;

	compare_cookies(skel->maps.src, dst_map);

out:
	test_sockmap_update__destroy(skel);
close_sk:
	close(sk);
}

static void test_sockmap_invalid_update(void)
{
	struct test_sockmap_invalid_update *skel;

	skel = test_sockmap_invalid_update__open_and_load();
	if (!ASSERT_NULL(skel, "open_and_load"))
		test_sockmap_invalid_update__destroy(skel);
}

static void test_sockmap_copy(enum bpf_map_type map_type)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	int err, len, src_fd, iter_fd;
	union bpf_iter_link_info linfo = {};
	__u32 i, num_sockets, num_elems;
	struct bpf_iter_sockmap *skel;
	__s64 *sock_fd = NULL;
	struct bpf_link *link;
	struct bpf_map *src;
	char buf[64];

	skel = bpf_iter_sockmap__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_sockmap__open_and_load"))
		return;

	if (map_type == BPF_MAP_TYPE_SOCKMAP) {
		src = skel->maps.sockmap;
		num_elems = bpf_map__max_entries(src);
		num_sockets = num_elems - 1;
	} else {
		src = skel->maps.sockhash;
		num_elems = bpf_map__max_entries(src) - 1;
		num_sockets = num_elems;
	}

	sock_fd = calloc(num_sockets, sizeof(*sock_fd));
	if (!ASSERT_OK_PTR(sock_fd, "calloc(sock_fd)"))
		goto out;

	for (i = 0; i < num_sockets; i++)
		sock_fd[i] = -1;

	src_fd = bpf_map__fd(src);

	for (i = 0; i < num_sockets; i++) {
		sock_fd[i] = connected_socket_v4();
		if (!ASSERT_NEQ(sock_fd[i], -1, "connected_socket_v4"))
			goto out;

		err = bpf_map_update_elem(src_fd, &i, &sock_fd[i], BPF_NOEXIST);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	linfo.map.map_fd = src_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.copy, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	/* do some tests */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	if (!ASSERT_GE(len, 0, "read"))
		goto close_iter;

	/* test results */
	if (!ASSERT_EQ(skel->bss->elems, num_elems, "elems"))
		goto close_iter;

	if (!ASSERT_EQ(skel->bss->socks, num_sockets, "socks"))
		goto close_iter;

	compare_cookies(src, skel->maps.dst);

close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
out:
	for (i = 0; sock_fd && i < num_sockets; i++)
		if (sock_fd[i] >= 0)
			close(sock_fd[i]);
	if (sock_fd)
		free(sock_fd);
	bpf_iter_sockmap__destroy(skel);
}

static void test_sockmap_skb_verdict_attach(enum bpf_attach_type first,
					    enum bpf_attach_type second)
{
	struct test_sockmap_skb_verdict_attach *skel;
	int err, map, verdict;

	skel = test_sockmap_skb_verdict_attach__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	map = bpf_map__fd(skel->maps.sock_map);

	err = bpf_prog_attach(verdict, map, first, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach"))
		goto out;

	err = bpf_prog_attach(verdict, map, second, 0);
	ASSERT_EQ(err, -EBUSY, "prog_attach_fail");

	err = bpf_prog_detach2(verdict, map, first);
	if (!ASSERT_OK(err, "bpf_prog_detach2"))
		goto out;
out:
	test_sockmap_skb_verdict_attach__destroy(skel);
}

static __u32 query_prog_id(int prog_fd)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int err;

	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (!ASSERT_OK(err, "bpf_obj_get_info_by_fd") ||
	    !ASSERT_EQ(info_len, sizeof(info), "bpf_obj_get_info_by_fd"))
		return 0;

	return info.id;
}

static void test_sockmap_progs_query(enum bpf_attach_type attach_type)
{
	struct test_sockmap_progs_query *skel;
	int err, map_fd, verdict_fd;
	__u32 attach_flags = 0;
	__u32 prog_ids[3] = {};
	__u32 prog_cnt = 3;

	skel = test_sockmap_progs_query__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_sockmap_progs_query__open_and_load"))
		return;

	map_fd = bpf_map__fd(skel->maps.sock_map);

	if (attach_type == BPF_SK_MSG_VERDICT)
		verdict_fd = bpf_program__fd(skel->progs.prog_skmsg_verdict);
	else
		verdict_fd = bpf_program__fd(skel->progs.prog_skb_verdict);

	err = bpf_prog_query(map_fd, attach_type, 0 /* query flags */,
			     &attach_flags, prog_ids, &prog_cnt);
	ASSERT_OK(err, "bpf_prog_query failed");
	ASSERT_EQ(attach_flags,  0, "wrong attach_flags on query");
	ASSERT_EQ(prog_cnt, 0, "wrong program count on query");

	err = bpf_prog_attach(verdict_fd, map_fd, attach_type, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach failed"))
		goto out;

	prog_cnt = 1;
	err = bpf_prog_query(map_fd, attach_type, 0 /* query flags */,
			     &attach_flags, prog_ids, &prog_cnt);
	ASSERT_OK(err, "bpf_prog_query failed");
	ASSERT_EQ(attach_flags, 0, "wrong attach_flags on query");
	ASSERT_EQ(prog_cnt, 1, "wrong program count on query");
	ASSERT_EQ(prog_ids[0], query_prog_id(verdict_fd),
		  "wrong prog_ids on query");

	bpf_prog_detach2(verdict_fd, map_fd, attach_type);
out:
	test_sockmap_progs_query__destroy(skel);
}

void test_sockmap_basic(void)
{
	if (test__start_subtest("sockmap create_update_free"))
		test_sockmap_create_update_free(BPF_MAP_TYPE_SOCKMAP);
	if (test__start_subtest("sockhash create_update_free"))
		test_sockmap_create_update_free(BPF_MAP_TYPE_SOCKHASH);
	if (test__start_subtest("sockmap sk_msg load helpers"))
		test_skmsg_helpers(BPF_MAP_TYPE_SOCKMAP);
	if (test__start_subtest("sockhash sk_msg load helpers"))
		test_skmsg_helpers(BPF_MAP_TYPE_SOCKHASH);
	if (test__start_subtest("sockmap update"))
		test_sockmap_update(BPF_MAP_TYPE_SOCKMAP);
	if (test__start_subtest("sockhash update"))
		test_sockmap_update(BPF_MAP_TYPE_SOCKHASH);
	if (test__start_subtest("sockmap update in unsafe context"))
		test_sockmap_invalid_update();
	if (test__start_subtest("sockmap copy"))
		test_sockmap_copy(BPF_MAP_TYPE_SOCKMAP);
	if (test__start_subtest("sockhash copy"))
		test_sockmap_copy(BPF_MAP_TYPE_SOCKHASH);
	if (test__start_subtest("sockmap skb_verdict attach")) {
		test_sockmap_skb_verdict_attach(BPF_SK_SKB_VERDICT,
						BPF_SK_SKB_STREAM_VERDICT);
		test_sockmap_skb_verdict_attach(BPF_SK_SKB_STREAM_VERDICT,
						BPF_SK_SKB_VERDICT);
	}
	if (test__start_subtest("sockmap msg_verdict progs query"))
		test_sockmap_progs_query(BPF_SK_MSG_VERDICT);
	if (test__start_subtest("sockmap stream_parser progs query"))
		test_sockmap_progs_query(BPF_SK_SKB_STREAM_PARSER);
	if (test__start_subtest("sockmap stream_verdict progs query"))
		test_sockmap_progs_query(BPF_SK_SKB_STREAM_VERDICT);
	if (test__start_subtest("sockmap skb_verdict progs query"))
		test_sockmap_progs_query(BPF_SK_SKB_VERDICT);
}
