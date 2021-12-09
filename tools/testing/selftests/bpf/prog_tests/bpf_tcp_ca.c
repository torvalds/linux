// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <linux/err.h>
#include <netinet/tcp.h>
#include <test_progs.h>
#include "network_helpers.h"
#include "bpf_dctcp.skel.h"
#include "bpf_cubic.skel.h"
#include "bpf_tcp_nogpl.skel.h"
#include "bpf_dctcp_release.skel.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif

static const unsigned int total_bytes = 10 * 1024 * 1024;
static int expected_stg = 0xeB9F;
static int stop, duration;

static int settcpca(int fd, const char *tcp_ca)
{
	int err;

	err = setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, tcp_ca, strlen(tcp_ca));
	if (CHECK(err == -1, "setsockopt(fd, TCP_CONGESTION)", "errno:%d\n",
		  errno))
		return -1;

	return 0;
}

static void *server(void *arg)
{
	int lfd = (int)(long)arg, err = 0, fd;
	ssize_t nr_sent = 0, bytes = 0;
	char batch[1500];

	fd = accept(lfd, NULL, NULL);
	while (fd == -1) {
		if (errno == EINTR)
			continue;
		err = -errno;
		goto done;
	}

	if (settimeo(fd, 0)) {
		err = -errno;
		goto done;
	}

	while (bytes < total_bytes && !READ_ONCE(stop)) {
		nr_sent = send(fd, &batch,
			       min(total_bytes - bytes, sizeof(batch)), 0);
		if (nr_sent == -1 && errno == EINTR)
			continue;
		if (nr_sent == -1) {
			err = -errno;
			break;
		}
		bytes += nr_sent;
	}

	CHECK(bytes != total_bytes, "send", "%zd != %u nr_sent:%zd errno:%d\n",
	      bytes, total_bytes, nr_sent, errno);

done:
	if (fd >= 0)
		close(fd);
	if (err) {
		WRITE_ONCE(stop, 1);
		return ERR_PTR(err);
	}
	return NULL;
}

static void do_test(const char *tcp_ca, const struct bpf_map *sk_stg_map)
{
	struct sockaddr_in6 sa6 = {};
	ssize_t nr_recv = 0, bytes = 0;
	int lfd = -1, fd = -1;
	pthread_t srv_thread;
	socklen_t addrlen = sizeof(sa6);
	void *thread_ret;
	char batch[1500];
	int err;

	WRITE_ONCE(stop, 0);

	lfd = socket(AF_INET6, SOCK_STREAM, 0);
	if (CHECK(lfd == -1, "socket", "errno:%d\n", errno))
		return;
	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (CHECK(fd == -1, "socket", "errno:%d\n", errno)) {
		close(lfd);
		return;
	}

	if (settcpca(lfd, tcp_ca) || settcpca(fd, tcp_ca) ||
	    settimeo(lfd, 0) || settimeo(fd, 0))
		goto done;

	/* bind, listen and start server thread to accept */
	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = in6addr_loopback;
	err = bind(lfd, (struct sockaddr *)&sa6, addrlen);
	if (CHECK(err == -1, "bind", "errno:%d\n", errno))
		goto done;
	err = getsockname(lfd, (struct sockaddr *)&sa6, &addrlen);
	if (CHECK(err == -1, "getsockname", "errno:%d\n", errno))
		goto done;
	err = listen(lfd, 1);
	if (CHECK(err == -1, "listen", "errno:%d\n", errno))
		goto done;

	if (sk_stg_map) {
		err = bpf_map_update_elem(bpf_map__fd(sk_stg_map), &fd,
					  &expected_stg, BPF_NOEXIST);
		if (CHECK(err, "bpf_map_update_elem(sk_stg_map)",
			  "err:%d errno:%d\n", err, errno))
			goto done;
	}

	/* connect to server */
	err = connect(fd, (struct sockaddr *)&sa6, addrlen);
	if (CHECK(err == -1, "connect", "errno:%d\n", errno))
		goto done;

	if (sk_stg_map) {
		int tmp_stg;

		err = bpf_map_lookup_elem(bpf_map__fd(sk_stg_map), &fd,
					  &tmp_stg);
		if (CHECK(!err || errno != ENOENT,
			  "bpf_map_lookup_elem(sk_stg_map)",
			  "err:%d errno:%d\n", err, errno))
			goto done;
	}

	err = pthread_create(&srv_thread, NULL, server, (void *)(long)lfd);
	if (CHECK(err != 0, "pthread_create", "err:%d errno:%d\n", err, errno))
		goto done;

	/* recv total_bytes */
	while (bytes < total_bytes && !READ_ONCE(stop)) {
		nr_recv = recv(fd, &batch,
			       min(total_bytes - bytes, sizeof(batch)), 0);
		if (nr_recv == -1 && errno == EINTR)
			continue;
		if (nr_recv == -1)
			break;
		bytes += nr_recv;
	}

	CHECK(bytes != total_bytes, "recv", "%zd != %u nr_recv:%zd errno:%d\n",
	      bytes, total_bytes, nr_recv, errno);

	WRITE_ONCE(stop, 1);
	pthread_join(srv_thread, &thread_ret);
	CHECK(IS_ERR(thread_ret), "pthread_join", "thread_ret:%ld",
	      PTR_ERR(thread_ret));
done:
	close(lfd);
	close(fd);
}

static void test_cubic(void)
{
	struct bpf_cubic *cubic_skel;
	struct bpf_link *link;

	cubic_skel = bpf_cubic__open_and_load();
	if (CHECK(!cubic_skel, "bpf_cubic__open_and_load", "failed\n"))
		return;

	link = bpf_map__attach_struct_ops(cubic_skel->maps.cubic);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops")) {
		bpf_cubic__destroy(cubic_skel);
		return;
	}

	do_test("bpf_cubic", NULL);

	bpf_link__destroy(link);
	bpf_cubic__destroy(cubic_skel);
}

static void test_dctcp(void)
{
	struct bpf_dctcp *dctcp_skel;
	struct bpf_link *link;

	dctcp_skel = bpf_dctcp__open_and_load();
	if (CHECK(!dctcp_skel, "bpf_dctcp__open_and_load", "failed\n"))
		return;

	link = bpf_map__attach_struct_ops(dctcp_skel->maps.dctcp);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops")) {
		bpf_dctcp__destroy(dctcp_skel);
		return;
	}

	do_test("bpf_dctcp", dctcp_skel->maps.sk_stg_map);
	CHECK(dctcp_skel->bss->stg_result != expected_stg,
	      "Unexpected stg_result", "stg_result (%x) != expected_stg (%x)\n",
	      dctcp_skel->bss->stg_result, expected_stg);

	bpf_link__destroy(link);
	bpf_dctcp__destroy(dctcp_skel);
}

static char *err_str;
static bool found;

static int libbpf_debug_print(enum libbpf_print_level level,
			      const char *format, va_list args)
{
	const char *log_buf;

	if (level != LIBBPF_WARN ||
	    !strstr(format, "-- BEGIN PROG LOAD LOG --")) {
		vprintf(format, args);
		return 0;
	}

	/* skip prog_name */
	va_arg(args, char *);
	log_buf = va_arg(args, char *);
	if (!log_buf)
		goto out;
	if (err_str && strstr(log_buf, err_str) != NULL)
		found = true;
out:
	printf(format, log_buf);
	return 0;
}

static void test_invalid_license(void)
{
	libbpf_print_fn_t old_print_fn;
	struct bpf_tcp_nogpl *skel;

	err_str = "struct ops programs must have a GPL compatible license";
	found = false;
	old_print_fn = libbpf_set_print(libbpf_debug_print);

	skel = bpf_tcp_nogpl__open_and_load();
	ASSERT_NULL(skel, "bpf_tcp_nogpl");
	ASSERT_EQ(found, true, "expected_err_msg");

	bpf_tcp_nogpl__destroy(skel);
	libbpf_set_print(old_print_fn);
}

static void test_dctcp_fallback(void)
{
	int err, lfd = -1, cli_fd = -1, srv_fd = -1;
	struct network_helper_opts opts = {
		.cc = "cubic",
	};
	struct bpf_dctcp *dctcp_skel;
	struct bpf_link *link = NULL;
	char srv_cc[16];
	socklen_t cc_len = sizeof(srv_cc);

	dctcp_skel = bpf_dctcp__open();
	if (!ASSERT_OK_PTR(dctcp_skel, "dctcp_skel"))
		return;
	strcpy(dctcp_skel->rodata->fallback, "cubic");
	if (!ASSERT_OK(bpf_dctcp__load(dctcp_skel), "bpf_dctcp__load"))
		goto done;

	link = bpf_map__attach_struct_ops(dctcp_skel->maps.dctcp);
	if (!ASSERT_OK_PTR(link, "dctcp link"))
		goto done;

	lfd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (!ASSERT_GE(lfd, 0, "lfd") ||
	    !ASSERT_OK(settcpca(lfd, "bpf_dctcp"), "lfd=>bpf_dctcp"))
		goto done;

	cli_fd = connect_to_fd_opts(lfd, &opts);
	if (!ASSERT_GE(cli_fd, 0, "cli_fd"))
		goto done;

	srv_fd = accept(lfd, NULL, 0);
	if (!ASSERT_GE(srv_fd, 0, "srv_fd"))
		goto done;
	ASSERT_STREQ(dctcp_skel->bss->cc_res, "cubic", "cc_res");
	ASSERT_EQ(dctcp_skel->bss->tcp_cdg_res, -ENOTSUPP, "tcp_cdg_res");

	err = getsockopt(srv_fd, SOL_TCP, TCP_CONGESTION, srv_cc, &cc_len);
	if (!ASSERT_OK(err, "getsockopt(srv_fd, TCP_CONGESTION)"))
		goto done;
	ASSERT_STREQ(srv_cc, "cubic", "srv_fd cc");

done:
	bpf_link__destroy(link);
	bpf_dctcp__destroy(dctcp_skel);
	if (lfd != -1)
		close(lfd);
	if (srv_fd != -1)
		close(srv_fd);
	if (cli_fd != -1)
		close(cli_fd);
}

static void test_rel_setsockopt(void)
{
	struct bpf_dctcp_release *rel_skel;
	libbpf_print_fn_t old_print_fn;

	err_str = "unknown func bpf_setsockopt";
	found = false;

	old_print_fn = libbpf_set_print(libbpf_debug_print);
	rel_skel = bpf_dctcp_release__open_and_load();
	libbpf_set_print(old_print_fn);

	ASSERT_ERR_PTR(rel_skel, "rel_skel");
	ASSERT_TRUE(found, "expected_err_msg");

	bpf_dctcp_release__destroy(rel_skel);
}

void test_bpf_tcp_ca(void)
{
	if (test__start_subtest("dctcp"))
		test_dctcp();
	if (test__start_subtest("cubic"))
		test_cubic();
	if (test__start_subtest("invalid_license"))
		test_invalid_license();
	if (test__start_subtest("dctcp_fallback"))
		test_dctcp_fallback();
	if (test__start_subtest("rel_setsockopt"))
		test_rel_setsockopt();
}
