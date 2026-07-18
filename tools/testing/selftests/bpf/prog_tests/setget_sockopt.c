// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <sched.h>
#include <linux/socket.h>
#include <linux/tls.h>
#include <net/if.h>

#include "test_progs.h"
#include "cgroup_helpers.h"
#include "network_helpers.h"

#include "setget_sockopt.skel.h"

#define CG_NAME "/setget-sockopt-test"

static const char addr4_str[] = "127.0.0.1";
static const char addr6_str[] = "::1";
static struct setget_sockopt *skel;
static int cg_fd;

static int create_netns(void)
{
	if (!ASSERT_OK(unshare(CLONE_NEWNET), "create netns"))
		return -1;

	if (!ASSERT_OK(system("ip link set dev lo up"), "set lo up"))
		return -1;

	if (!ASSERT_OK(system("ip link add dev binddevtest1 type veth peer name binddevtest2"),
		       "add veth"))
		return -1;

	if (!ASSERT_OK(system("ip link set dev binddevtest1 up"),
		       "bring veth up"))
		return -1;

	return 0;
}

static void test_tcp(int family)
{
	struct setget_sockopt__bss *bss = skel->bss;
	int sfd, cfd;

	memset(bss, 0, sizeof(*bss));

	sfd = start_server(family, SOCK_STREAM,
			   family == AF_INET6 ? addr6_str : addr4_str, 0, 0);
	if (!ASSERT_GE(sfd, 0, "start_server"))
		return;

	cfd = connect_to_fd(sfd, 0);
	if (!ASSERT_GE(cfd, 0, "connect_to_fd_server")) {
		close(sfd);
		return;
	}
	close(sfd);
	close(cfd);

	ASSERT_EQ(bss->nr_listen, 1, "nr_listen");
	ASSERT_EQ(bss->nr_connect, 1, "nr_connect");
	ASSERT_EQ(bss->nr_active, 1, "nr_active");
	ASSERT_EQ(bss->nr_passive, 1, "nr_passive");
	ASSERT_EQ(bss->nr_socket_post_create, 2, "nr_socket_post_create");
	ASSERT_EQ(bss->nr_binddev, 2, "nr_bind");
}

static void test_udp(int family)
{
	struct setget_sockopt__bss *bss = skel->bss;
	int sfd;

	memset(bss, 0, sizeof(*bss));

	sfd = start_server(family, SOCK_DGRAM,
			   family == AF_INET6 ? addr6_str : addr4_str, 0, 0);
	if (!ASSERT_GE(sfd, 0, "start_server"))
		return;
	close(sfd);

	ASSERT_GE(bss->nr_socket_post_create, 1, "nr_socket_post_create");
	ASSERT_EQ(bss->nr_binddev, 1, "nr_bind");
}

static void test_ktls(int family)
{
	struct tls12_crypto_info_aes_gcm_128 aes128;
	struct setget_sockopt__bss *bss = skel->bss;
	int cfd = -1, sfd = -1, fd = -1, ret;
	char buf;

	memset(bss, 0, sizeof(*bss));

	sfd = start_server(family, SOCK_STREAM,
			   family == AF_INET6 ? addr6_str : addr4_str, 0, 0);
	if (!ASSERT_GE(sfd, 0, "start_server"))
		return;
	fd = connect_to_fd(sfd, 0);
	if (!ASSERT_GE(fd, 0, "connect_to_fd"))
		goto err_out;

	cfd = accept(sfd, NULL, 0);
	if (!ASSERT_GE(cfd, 0, "accept"))
		goto err_out;

	close(sfd);
	sfd = -1;

	/* Setup KTLS */
	ret = setsockopt(fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	if (!ASSERT_OK(ret, "setsockopt"))
		goto err_out;
	ret = setsockopt(cfd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	if (!ASSERT_OK(ret, "setsockopt"))
		goto err_out;

	memset(&aes128, 0, sizeof(aes128));
	aes128.info.version = TLS_1_2_VERSION;
	aes128.info.cipher_type = TLS_CIPHER_AES_GCM_128;

	ret = setsockopt(fd, SOL_TLS, TLS_TX, &aes128, sizeof(aes128));
	if (!ASSERT_OK(ret, "setsockopt"))
		goto err_out;

	ret = setsockopt(cfd, SOL_TLS, TLS_RX, &aes128, sizeof(aes128));
	if (!ASSERT_OK(ret, "setsockopt"))
		goto err_out;

	/* KTLS is enabled */

	close(fd);
	/* At this point, the cfd socket is at the CLOSE_WAIT state
	 * and still run TLS protocol.  The test for
	 * BPF_TCP_CLOSE_WAIT should be run at this point.
	 */
	ret = read(cfd, &buf, sizeof(buf));
	ASSERT_EQ(ret, 0, "read");
	close(cfd);

	ASSERT_EQ(bss->nr_listen, 1, "nr_listen");
	ASSERT_EQ(bss->nr_connect, 1, "nr_connect");
	ASSERT_EQ(bss->nr_active, 1, "nr_active");
	ASSERT_EQ(bss->nr_passive, 1, "nr_passive");
	ASSERT_EQ(bss->nr_socket_post_create, 2, "nr_socket_post_create");
	ASSERT_EQ(bss->nr_binddev, 2, "nr_bind");
	ASSERT_EQ(bss->nr_fin_wait1, 1, "nr_fin_wait1");
	return;

err_out:
	close(fd);
	close(cfd);
	close(sfd);
}

static void test_nonstandard_opt(int family)
{
	struct setget_sockopt__bss *bss = skel->bss;
	struct bpf_link *getsockopt_link = NULL;
	int sfd = -1, fd = -1, cfd = -1, flags;
	socklen_t flagslen = sizeof(flags);

	memset(bss, 0, sizeof(*bss));

	sfd = start_server(family, SOCK_STREAM,
			   family == AF_INET6 ? addr6_str : addr4_str, 0, 0);
	if (!ASSERT_GE(sfd, 0, "start_server"))
		return;

	fd = connect_to_fd(sfd, 0);
	if (!ASSERT_GE(fd, 0, "connect_to_fd_server"))
		goto err_out;

	/* cgroup/getsockopt prog will intercept getsockopt() below and
	 * retrieve the tcp socket bpf_sock_ops_cb_flags value for the
	 * accept()ed socket; this was set earlier in the passive established
	 * callback for the accept()ed socket via bpf_setsockopt().
	 */
	getsockopt_link = bpf_program__attach_cgroup(skel->progs._getsockopt, cg_fd);
	if (!ASSERT_OK_PTR(getsockopt_link, "getsockopt prog"))
		goto err_out;

	cfd = accept(sfd, NULL, 0);
	if (!ASSERT_GE(cfd, 0, "accept"))
		goto err_out;

	if (!ASSERT_OK(getsockopt(cfd, SOL_TCP, TCP_BPF_SOCK_OPS_CB_FLAGS, &flags, &flagslen),
		       "getsockopt_flags"))
		goto err_out;
	ASSERT_EQ(flags & BPF_SOCK_OPS_STATE_CB_FLAG, BPF_SOCK_OPS_STATE_CB_FLAG,
		  "cb_flags_set");
err_out:
	close(sfd);
	if (fd != -1)
		close(fd);
	if (cfd != -1)
		close(cfd);
	bpf_link__destroy(getsockopt_link);
}

static int connect_to_v4mapped_v6_fd(int server_fd)
{
	struct sockaddr_storage addr;
	struct sockaddr_in *addr4 = (void *)&addr;
	socklen_t addrlen = sizeof(addr);
	struct sockaddr_in6 addr6 = {};
	int fd = -1, v6only = 0, err;

	err = getsockname(server_fd, (struct sockaddr *)&addr, &addrlen);
	if (!ASSERT_OK(err, "getsockname"))
		return -1;

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (!ASSERT_GE(fd, 0, "socket"))
		return -1;

	err = settimeo(fd, 0);
	if (!ASSERT_OK(err, "settimeo"))
		goto err_out;

	err = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
	if (!ASSERT_OK(err, "clear_v6only"))
		goto err_out;

	addr6.sin6_family = AF_INET6;
	addr6.sin6_port = addr4->sin_port;
	addr6.sin6_addr.s6_addr[10] = 0xff;
	addr6.sin6_addr.s6_addr[11] = 0xff;
	memcpy(&addr6.sin6_addr.s6_addr[12], &addr4->sin_addr, sizeof(addr4->sin_addr));

	err = connect(fd, (struct sockaddr *)&addr6, sizeof(addr6));
	if (!ASSERT_OK(err, "connect"))
		goto err_out;

	return fd;

err_out:
	close(fd);
	return -1;
}

static void test_v4mapped_v6_ip_tos(void)
{
	struct setget_sockopt__bss *bss = skel->bss;
	int sfd = -1, fd = -1, got = 0, exp = 0x1c;
	socklen_t optlen;

	memset(bss, 0, sizeof(*bss));
	bss->v4mapped_v6_ip_tos_enable = 1;
	bss->v4mapped_v6_ip_tos_ret = -1;
	bss->v4mapped_v6_ip_tos_val = exp;

	sfd = start_server(AF_INET, SOCK_STREAM, addr4_str, 0, 0);
	if (!ASSERT_GE(sfd, 0, "start_server"))
		goto err_out;

	fd = connect_to_v4mapped_v6_fd(sfd);
	if (!ASSERT_GE(fd, 0, "connect_to_v4mapped_v6_fd"))
		goto err_out;

	ASSERT_GT(bss->v4mapped_v6_ip_tos_cnt, 0, "v4mapped_v6_ip_tos_cnt");
	ASSERT_EQ(bss->v4mapped_v6_ip_tos_ret, 0, "v4mapped_v6_ip_tos_ret");

	optlen = sizeof(got);
	if (!ASSERT_OK(getsockopt(fd, SOL_IP, IP_TOS, &got, &optlen), "getsockopt_ip_tos"))
		goto err_out;

	ASSERT_EQ(got, exp, "ip_tos");

err_out:
	bss->v4mapped_v6_ip_tos_enable = 0;
	if (fd >= 0)
		close(fd);
	if (sfd >= 0)
		close(sfd);
}

void test_setget_sockopt(void)
{
	cg_fd = test__join_cgroup(CG_NAME);
	if (!ASSERT_OK_FD(cg_fd, "join cgroup"))
		return;

	if (create_netns())
		goto done;

	skel = setget_sockopt__open();
	if (!ASSERT_OK_PTR(skel, "open skel"))
		goto done;

	strscpy(skel->rodata->veth, "binddevtest1");
	skel->rodata->veth_ifindex = if_nametoindex("binddevtest1");
	if (!ASSERT_GT(skel->rodata->veth_ifindex, 0, "if_nametoindex"))
		goto done;

	if (!ASSERT_OK(setget_sockopt__load(skel), "load skel"))
		goto done;

	skel->links.skops_sockopt =
		bpf_program__attach_cgroup(skel->progs.skops_sockopt, cg_fd);
	if (!ASSERT_OK_PTR(skel->links.skops_sockopt, "attach cgroup"))
		goto done;

	skel->links.socket_post_create =
		bpf_program__attach_cgroup(skel->progs.socket_post_create, cg_fd);
	if (!ASSERT_OK_PTR(skel->links.socket_post_create, "attach_cgroup"))
		goto done;

	test_tcp(AF_INET6);
	test_tcp(AF_INET);
	test_udp(AF_INET6);
	test_udp(AF_INET);
	test_ktls(AF_INET6);
	test_ktls(AF_INET);
	test_nonstandard_opt(AF_INET);
	test_nonstandard_opt(AF_INET6);
	test_v4mapped_v6_ip_tos();

done:
	setget_sockopt__destroy(skel);
	close(cg_fd);
}
