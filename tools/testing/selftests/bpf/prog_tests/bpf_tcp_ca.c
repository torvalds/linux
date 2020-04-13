// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <linux/err.h>
#include <test_progs.h>
#include "bpf_dctcp.skel.h"
#include "bpf_cubic.skel.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

static const unsigned int total_bytes = 10 * 1024 * 1024;
static const struct timeval timeo_sec = { .tv_sec = 10 };
static const size_t timeo_optlen = sizeof(timeo_sec);
static int expected_stg = 0xeB9F;
static int stop, duration;

static int settimeo(int fd)
{
	int err;

	err = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeo_sec,
			 timeo_optlen);
	if (CHECK(err == -1, "setsockopt(fd, SO_RCVTIMEO)", "errno:%d\n",
		  errno))
		return -1;

	err = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeo_sec,
			 timeo_optlen);
	if (CHECK(err == -1, "setsockopt(fd, SO_SNDTIMEO)", "errno:%d\n",
		  errno))
		return -1;

	return 0;
}

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

	if (settimeo(fd)) {
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
	if (fd != -1)
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
	    settimeo(lfd) || settimeo(fd))
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
	if (CHECK(IS_ERR(link), "bpf_map__attach_struct_ops", "err:%ld\n",
		  PTR_ERR(link))) {
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
	if (CHECK(IS_ERR(link), "bpf_map__attach_struct_ops", "err:%ld\n",
		  PTR_ERR(link))) {
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

void test_bpf_tcp_ca(void)
{
	if (test__start_subtest("dctcp"))
		test_dctcp();
	if (test__start_subtest("cubic"))
		test_cubic();
}
