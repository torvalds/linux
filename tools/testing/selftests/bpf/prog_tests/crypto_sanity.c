// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/in6.h>
#include <linux/if_alg.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "crypto_sanity.skel.h"
#include "crypto_basic.skel.h"

#define NS_TEST "crypto_sanity_ns"
#define IPV6_IFACE_ADDR "face::1"
static const unsigned char crypto_key[] = "testtest12345678";
static const char plain_text[] = "stringtoencrypt0";
static int opfd = -1, tfmfd = -1;
static const char algo[] = "ecb(aes)";
static int init_afalg(void)
{
	struct sockaddr_alg sa = {
		.salg_family = AF_ALG,
		.salg_type = "skcipher",
		.salg_name = "ecb(aes)"
	};

	tfmfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (tfmfd == -1)
		return errno;
	if (bind(tfmfd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
		return errno;
	if (setsockopt(tfmfd, SOL_ALG, ALG_SET_KEY, crypto_key, 16) == -1)
		return errno;
	opfd = accept(tfmfd, NULL, 0);
	if (opfd == -1)
		return errno;
	return 0;
}

static void deinit_afalg(void)
{
	if (tfmfd != -1)
		close(tfmfd);
	if (opfd != -1)
		close(opfd);
}

static void do_crypt_afalg(const void *src, void *dst, int size, bool encrypt)
{
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	char cbuf[CMSG_SPACE(4)] = {0};
	struct iovec iov;

	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof(cbuf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_ALG;
	cmsg->cmsg_type = ALG_SET_OP;
	cmsg->cmsg_len = CMSG_LEN(4);
	*(__u32 *)CMSG_DATA(cmsg) = encrypt ? ALG_OP_ENCRYPT : ALG_OP_DECRYPT;

	iov.iov_base = (char *)src;
	iov.iov_len = size;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	sendmsg(opfd, &msg, 0);
	read(opfd, dst, size);
}

void test_crypto_basic(void)
{
	RUN_TESTS(crypto_basic);
}

void test_crypto_sanity(void)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_hook, .attach_point = BPF_TC_EGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_attach_enc);
	LIBBPF_OPTS(bpf_tc_opts, tc_attach_dec);
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct nstoken *nstoken = NULL;
	struct crypto_sanity *skel;
	char afalg_plain[16] = {0};
	char afalg_dst[16] = {0};
	struct sockaddr_in6 addr;
	int sockfd, err, pfd;
	socklen_t addrlen;
	u16 udp_test_port;

	skel = crypto_sanity__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open"))
		return;

	SYS(fail, "ip netns add %s", NS_TEST);
	SYS(fail, "ip -net %s -6 addr add %s/128 dev lo nodad", NS_TEST, IPV6_IFACE_ADDR);
	SYS(fail, "ip -net %s link set dev lo up", NS_TEST);

	nstoken = open_netns(NS_TEST);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto fail;

	err = init_afalg();
	if (!ASSERT_OK(err, "AF_ALG init fail"))
		goto fail;

	qdisc_hook.ifindex = if_nametoindex("lo");
	if (!ASSERT_GT(qdisc_hook.ifindex, 0, "if_nametoindex lo"))
		goto fail;

	skel->bss->key_len = 16;
	skel->bss->authsize = 0;
	udp_test_port = skel->data->udp_test_port;
	memcpy(skel->bss->key, crypto_key, sizeof(crypto_key));
	snprintf(skel->bss->algo, 128, "%s", algo);
	pfd = bpf_program__fd(skel->progs.skb_crypto_setup);
	if (!ASSERT_GT(pfd, 0, "skb_crypto_setup fd"))
		goto fail;

	err = bpf_prog_test_run_opts(pfd, &opts);
	if (!ASSERT_OK(err, "skb_crypto_setup") ||
	    !ASSERT_OK(opts.retval, "skb_crypto_setup retval"))
		goto fail;

	if (!ASSERT_OK(skel->bss->status, "skb_crypto_setup status"))
		goto fail;

	err = bpf_tc_hook_create(&qdisc_hook);
	if (!ASSERT_OK(err, "create qdisc hook"))
		goto fail;

	addrlen = sizeof(addr);
	err = make_sockaddr(AF_INET6, IPV6_IFACE_ADDR, udp_test_port,
			    (void *)&addr, &addrlen);
	if (!ASSERT_OK(err, "make_sockaddr"))
		goto fail;

	tc_attach_enc.prog_fd = bpf_program__fd(skel->progs.encrypt_sanity);
	err = bpf_tc_attach(&qdisc_hook, &tc_attach_enc);
	if (!ASSERT_OK(err, "attach encrypt filter"))
		goto fail;

	sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (!ASSERT_NEQ(sockfd, -1, "encrypt socket"))
		goto fail;
	err = sendto(sockfd, plain_text, sizeof(plain_text), 0, (void *)&addr, addrlen);
	close(sockfd);
	if (!ASSERT_EQ(err, sizeof(plain_text), "encrypt send"))
		goto fail;

	do_crypt_afalg(plain_text, afalg_dst, sizeof(afalg_dst), true);

	if (!ASSERT_OK(skel->bss->status, "encrypt status"))
		goto fail;
	if (!ASSERT_STRNEQ(skel->bss->dst, afalg_dst, sizeof(afalg_dst), "encrypt AF_ALG"))
		goto fail;

	tc_attach_enc.flags = tc_attach_enc.prog_fd = tc_attach_enc.prog_id = 0;
	err = bpf_tc_detach(&qdisc_hook, &tc_attach_enc);
	if (!ASSERT_OK(err, "bpf_tc_detach encrypt"))
		goto fail;

	tc_attach_dec.prog_fd = bpf_program__fd(skel->progs.decrypt_sanity);
	err = bpf_tc_attach(&qdisc_hook, &tc_attach_dec);
	if (!ASSERT_OK(err, "attach decrypt filter"))
		goto fail;

	sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (!ASSERT_NEQ(sockfd, -1, "decrypt socket"))
		goto fail;
	err = sendto(sockfd, afalg_dst, sizeof(afalg_dst), 0, (void *)&addr, addrlen);
	close(sockfd);
	if (!ASSERT_EQ(err, sizeof(afalg_dst), "decrypt send"))
		goto fail;

	do_crypt_afalg(afalg_dst, afalg_plain, sizeof(afalg_plain), false);

	if (!ASSERT_OK(skel->bss->status, "decrypt status"))
		goto fail;
	if (!ASSERT_STRNEQ(skel->bss->dst, afalg_plain, sizeof(afalg_plain), "decrypt AF_ALG"))
		goto fail;

	tc_attach_dec.flags = tc_attach_dec.prog_fd = tc_attach_dec.prog_id = 0;
	err = bpf_tc_detach(&qdisc_hook, &tc_attach_dec);
	ASSERT_OK(err, "bpf_tc_detach decrypt");

fail:
	close_netns(nstoken);
	deinit_afalg();
	SYS_NOFAIL("ip netns del " NS_TEST " &> /dev/null");
	crypto_sanity__destroy(skel);
}
