// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
#include <error.h>
#include <netinet/tcp.h>

#include "test_progs.h"
#include "test_skmsg_load_helpers.skel.h"
#include "test_sockmap_update.skel.h"
#include "test_sockmap_invalid_update.skel.h"
#include "test_sockmap_skb_verdict_attach.skel.h"
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
	if (CHECK_FAIL(s == -1))
		goto error;

	repair = TCP_REPAIR_ON;
	err = setsockopt(s, SOL_TCP, TCP_REPAIR, &repair, sizeof(repair));
	if (CHECK_FAIL(err))
		goto error;

	err = connect(s, (struct sockaddr *)&addr, len);
	if (CHECK_FAIL(err))
		goto error;

	repair = TCP_REPAIR_OFF_NO_WP;
	err = setsockopt(s, SOL_TCP, TCP_REPAIR, &repair, sizeof(repair));
	if (CHECK_FAIL(err))
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
	int err, duration = 0, src_fd, dst_fd;

	src_fd = bpf_map__fd(src);
	dst_fd = bpf_map__fd(dst);

	for (i = 0; i < max_entries; i++) {
		__u64 src_cookie, dst_cookie;

		err = bpf_map_lookup_elem(src_fd, &i, &src_cookie);
		if (err && errno == ENOENT) {
			err = bpf_map_lookup_elem(dst_fd, &i, &dst_cookie);
			CHECK(!err, "map_lookup_elem(dst)", "element %u not deleted\n", i);
			CHECK(err && errno != ENOENT, "map_lookup_elem(dst)", "%s\n",
			      strerror(errno));
			continue;
		}
		if (CHECK(err, "lookup_elem(src)", "%s\n", strerror(errno)))
			continue;

		err = bpf_map_lookup_elem(dst_fd, &i, &dst_cookie);
		if (CHECK(err, "lookup_elem(dst)", "%s\n", strerror(errno)))
			continue;

		CHECK(dst_cookie != src_cookie, "cookie mismatch",
		      "%llu != %llu (pos %u)\n", dst_cookie, src_cookie, i);
	}
}

/* Create a map, populate it with one socket, and free the map. */
static void test_sockmap_create_update_free(enum bpf_map_type map_type)
{
	const int zero = 0;
	int s, map, err;

	s = connected_socket_v4();
	if (CHECK_FAIL(s < 0))
		return;

	map = bpf_map_create(map_type, NULL, sizeof(int), sizeof(int), 1, NULL);
	if (CHECK_FAIL(map < 0)) {
		perror("bpf_cmap_create");
		goto out;
	}

	err = bpf_map_update_elem(map, &zero, &s, BPF_NOEXIST);
	if (CHECK_FAIL(err)) {
		perror("bpf_map_update");
		goto out;
	}

out:
	close(map);
	close(s);
}

static void test_skmsg_helpers(enum bpf_map_type map_type)
{
	struct test_skmsg_load_helpers *skel;
	int err, map, verdict;

	skel = test_skmsg_load_helpers__open_and_load();
	if (CHECK_FAIL(!skel)) {
		perror("test_skmsg_load_helpers__open_and_load");
		return;
	}

	verdict = bpf_program__fd(skel->progs.prog_msg_verdict);
	map = bpf_map__fd(skel->maps.sock_map);

	err = bpf_prog_attach(verdict, map, BPF_SK_MSG_VERDICT, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach");
		goto out;
	}

	err = bpf_prog_detach2(verdict, map, BPF_SK_MSG_VERDICT);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_detach2");
		goto out;
	}
out:
	test_skmsg_load_helpers__destroy(skel);
}

static void test_sockmap_update(enum bpf_map_type map_type)
{
	struct bpf_prog_test_run_attr tattr;
	int err, prog, src, duration = 0;
	struct test_sockmap_update *skel;
	struct bpf_map *dst_map;
	const __u32 zero = 0;
	char dummy[14] = {0};
	__s64 sk;

	sk = connected_socket_v4();
	if (CHECK(sk == -1, "connected_socket_v4", "cannot connect\n"))
		return;

	skel = test_sockmap_update__open_and_load();
	if (CHECK(!skel, "open_and_load", "cannot load skeleton\n"))
		goto close_sk;

	prog = bpf_program__fd(skel->progs.copy_sock_map);
	src = bpf_map__fd(skel->maps.src);
	if (map_type == BPF_MAP_TYPE_SOCKMAP)
		dst_map = skel->maps.dst_sock_map;
	else
		dst_map = skel->maps.dst_sock_hash;

	err = bpf_map_update_elem(src, &zero, &sk, BPF_NOEXIST);
	if (CHECK(err, "update_elem(src)", "errno=%u\n", errno))
		goto out;

	tattr = (struct bpf_prog_test_run_attr){
		.prog_fd = prog,
		.repeat = 1,
		.data_in = dummy,
		.data_size_in = sizeof(dummy),
	};

	err = bpf_prog_test_run_xattr(&tattr);
	if (CHECK_ATTR(err || !tattr.retval, "bpf_prog_test_run",
		       "errno=%u retval=%u\n", errno, tattr.retval))
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
	int duration = 0;

	skel = test_sockmap_invalid_update__open_and_load();
	if (CHECK(skel, "open_and_load", "verifier accepted map_update\n"))
		test_sockmap_invalid_update__destroy(skel);
}

static void test_sockmap_copy(enum bpf_map_type map_type)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	int err, len, src_fd, iter_fd, duration = 0;
	union bpf_iter_link_info linfo = {};
	__u32 i, num_sockets, num_elems;
	struct bpf_iter_sockmap *skel;
	__s64 *sock_fd = NULL;
	struct bpf_link *link;
	struct bpf_map *src;
	char buf[64];

	skel = bpf_iter_sockmap__open_and_load();
	if (CHECK(!skel, "bpf_iter_sockmap__open_and_load", "skeleton open_and_load failed\n"))
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
	if (CHECK(!sock_fd, "calloc(sock_fd)", "failed to allocate\n"))
		goto out;

	for (i = 0; i < num_sockets; i++)
		sock_fd[i] = -1;

	src_fd = bpf_map__fd(src);

	for (i = 0; i < num_sockets; i++) {
		sock_fd[i] = connected_socket_v4();
		if (CHECK(sock_fd[i] == -1, "connected_socket_v4", "cannot connect\n"))
			goto out;

		err = bpf_map_update_elem(src_fd, &i, &sock_fd[i], BPF_NOEXIST);
		if (CHECK(err, "map_update", "failed: %s\n", strerror(errno)))
			goto out;
	}

	linfo.map.map_fd = src_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.copy, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (CHECK(iter_fd < 0, "create_iter", "create_iter failed\n"))
		goto free_link;

	/* do some tests */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	if (CHECK(len < 0, "read", "failed: %s\n", strerror(errno)))
		goto close_iter;

	/* test results */
	if (CHECK(skel->bss->elems != num_elems, "elems", "got %u expected %u\n",
		  skel->bss->elems, num_elems))
		goto close_iter;

	if (CHECK(skel->bss->socks != num_sockets, "socks", "got %u expected %u\n",
		  skel->bss->socks, num_sockets))
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
	if (CHECK_FAIL(!skel)) {
		perror("test_sockmap_skb_verdict_attach__open_and_load");
		return;
	}

	verdict = bpf_program__fd(skel->progs.prog_skb_verdict);
	map = bpf_map__fd(skel->maps.sock_map);

	err = bpf_prog_attach(verdict, map, first, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach");
		goto out;
	}

	err = bpf_prog_attach(verdict, map, second, 0);
	ASSERT_EQ(err, -EBUSY, "prog_attach_fail");

	err = bpf_prog_detach2(verdict, map, first);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_detach2");
		goto out;
	}
out:
	test_sockmap_skb_verdict_attach__destroy(skel);
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
}
