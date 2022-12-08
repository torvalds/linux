// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "cgroup_helpers.h"

#include <linux/tcp.h>
#include "sockopt_sk.skel.h"

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

#define SOL_CUSTOM			0xdeadbeef

static int getsetsockopt(void)
{
	int fd, err;
	union {
		char u8[4];
		__u32 u32;
		char cc[16]; /* TCP_CA_NAME_MAX */
		struct tcp_zerocopy_receive zc;
	} buf = {};
	socklen_t optlen;
	char *big_buf = NULL;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create socket");
		return -1;
	}

	/* IP_TOS - BPF bypass */

	optlen = getpagesize() * 2;
	big_buf = calloc(1, optlen);
	if (!big_buf) {
		log_err("Couldn't allocate two pages");
		goto err;
	}

	*(int *)big_buf = 0x08;
	err = setsockopt(fd, SOL_IP, IP_TOS, big_buf, optlen);
	if (err) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto err;
	}

	memset(big_buf, 0, optlen);
	optlen = 1;
	err = getsockopt(fd, SOL_IP, IP_TOS, big_buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto err;
	}

	if (*big_buf != 0x08) {
		log_err("Unexpected getsockopt(IP_TOS) optval 0x%x != 0x08",
			(int)*big_buf);
		goto err;
	}

	/* IP_TTL - EPERM */

	buf.u8[0] = 1;
	err = setsockopt(fd, SOL_IP, IP_TTL, &buf, 1);
	if (!err || errno != EPERM) {
		log_err("Unexpected success from setsockopt(IP_TTL)");
		goto err;
	}

	/* SOL_CUSTOM - handled by BPF */

	buf.u8[0] = 0x01;
	err = setsockopt(fd, SOL_CUSTOM, 0, &buf, 1);
	if (err) {
		log_err("Failed to call setsockopt");
		goto err;
	}

	buf.u32 = 0x00;
	optlen = 4;
	err = getsockopt(fd, SOL_CUSTOM, 0, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt");
		goto err;
	}

	if (optlen != 1) {
		log_err("Unexpected optlen %d != 1", optlen);
		goto err;
	}
	if (buf.u8[0] != 0x01) {
		log_err("Unexpected buf[0] 0x%02x != 0x01", buf.u8[0]);
		goto err;
	}

	/* IP_FREEBIND - BPF can't access optval past PAGE_SIZE */

	optlen = getpagesize() * 2;
	memset(big_buf, 0, optlen);

	err = setsockopt(fd, SOL_IP, IP_FREEBIND, big_buf, optlen);
	if (err != 0) {
		log_err("Failed to call setsockopt, ret=%d", err);
		goto err;
	}

	err = getsockopt(fd, SOL_IP, IP_FREEBIND, big_buf, &optlen);
	if (err != 0) {
		log_err("Failed to call getsockopt, ret=%d", err);
		goto err;
	}

	if (optlen != 1 || *(__u8 *)big_buf != 0x55) {
		log_err("Unexpected IP_FREEBIND getsockopt, optlen=%d, optval=0x%x",
			optlen, *(__u8 *)big_buf);
	}

	/* SO_SNDBUF is overwritten */

	buf.u32 = 0x01010101;
	err = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, 4);
	if (err) {
		log_err("Failed to call setsockopt(SO_SNDBUF)");
		goto err;
	}

	buf.u32 = 0x00;
	optlen = 4;
	err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(SO_SNDBUF)");
		goto err;
	}

	if (buf.u32 != 0x55AA*2) {
		log_err("Unexpected getsockopt(SO_SNDBUF) 0x%x != 0x55AA*2",
			buf.u32);
		goto err;
	}

	/* TCP_CONGESTION can extend the string */

	strcpy(buf.cc, "nv");
	err = setsockopt(fd, SOL_TCP, TCP_CONGESTION, &buf, strlen("nv"));
	if (err) {
		log_err("Failed to call setsockopt(TCP_CONGESTION)");
		goto err;
	}


	optlen = sizeof(buf.cc);
	err = getsockopt(fd, SOL_TCP, TCP_CONGESTION, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(TCP_CONGESTION)");
		goto err;
	}

	if (strcmp(buf.cc, "cubic") != 0) {
		log_err("Unexpected getsockopt(TCP_CONGESTION) %s != %s",
			buf.cc, "cubic");
		goto err;
	}

	/* TCP_ZEROCOPY_RECEIVE triggers */
	memset(&buf, 0, sizeof(buf));
	optlen = sizeof(buf.zc);
	err = getsockopt(fd, SOL_TCP, TCP_ZEROCOPY_RECEIVE, &buf, &optlen);
	if (err) {
		log_err("Unexpected getsockopt(TCP_ZEROCOPY_RECEIVE) err=%d errno=%d",
			err, errno);
		goto err;
	}

	memset(&buf, 0, sizeof(buf));
	buf.zc.address = 12345; /* Not page aligned. Rejected by tcp_zerocopy_receive() */
	optlen = sizeof(buf.zc);
	errno = 0;
	err = getsockopt(fd, SOL_TCP, TCP_ZEROCOPY_RECEIVE, &buf, &optlen);
	if (errno != EINVAL) {
		log_err("Unexpected getsockopt(TCP_ZEROCOPY_RECEIVE) err=%d errno=%d",
			err, errno);
		goto err;
	}

	free(big_buf);
	close(fd);
	return 0;
err:
	free(big_buf);
	close(fd);
	return -1;
}

static void run_test(int cgroup_fd)
{
	struct sockopt_sk *skel;

	skel = sockopt_sk__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	skel->bss->page_size = getpagesize();

	skel->links._setsockopt =
		bpf_program__attach_cgroup(skel->progs._setsockopt, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links._setsockopt, "setsockopt_link"))
		goto cleanup;

	skel->links._getsockopt =
		bpf_program__attach_cgroup(skel->progs._getsockopt, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links._getsockopt, "getsockopt_link"))
		goto cleanup;

	ASSERT_OK(getsetsockopt(), "getsetsockopt");

cleanup:
	sockopt_sk__destroy(skel);
}

void test_sockopt_sk(void)
{
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/sockopt_sk");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup /sockopt_sk"))
		return;

	run_test(cgroup_fd);
	close(cgroup_fd);
}
