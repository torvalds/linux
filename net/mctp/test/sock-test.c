// SPDX-License-Identifier: GPL-2.0

#include <kunit/static_stub.h>
#include <kunit/test.h>

#include <linux/socket.h>
#include <linux/spinlock.h>

#include "utils.h"

static const u8 dev_default_lladdr[] = { 0x01, 0x02 };

/* helper for simple sock setup: single device, with dev_default_lladdr as its
 * hardware address, assigned with a local EID 8, and a route to EID 9
 */
static void __mctp_sock_test_init(struct kunit *test,
				  struct mctp_test_dev **devp,
				  struct mctp_test_route **rtp,
				  struct socket **sockp)
{
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct socket *sock;
	unsigned long flags;
	u8 *addrs;
	int rc;

	dev = mctp_test_create_dev_lladdr(sizeof(dev_default_lladdr),
					  dev_default_lladdr);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	addrs = kmalloc(1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, addrs);
	addrs[0] = 8;

	spin_lock_irqsave(&dev->mdev->addrs_lock, flags);
	dev->mdev->num_addrs = 1;
	swap(addrs, dev->mdev->addrs);
	spin_unlock_irqrestore(&dev->mdev->addrs_lock, flags);

	kfree(addrs);

	rt = mctp_test_create_route_direct(dev_net(dev->ndev), dev->mdev, 9, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	*devp = dev;
	*rtp = rt;
	*sockp = sock;
}

static void __mctp_sock_test_fini(struct kunit *test,
				  struct mctp_test_dev *dev,
				  struct mctp_test_route *rt,
				  struct socket *sock)
{
	sock_release(sock);
	mctp_test_route_destroy(test, rt);
	mctp_test_destroy_dev(dev);
}

struct mctp_test_sock_local_output_config {
	struct mctp_test_dev *dev;
	size_t halen;
	u8 haddr[MAX_ADDR_LEN];
	bool invoked;
	int rc;
};

static int mctp_test_sock_local_output(struct sock *sk,
				       struct mctp_dst *dst,
				       struct sk_buff *skb,
				       mctp_eid_t daddr, u8 req_tag)
{
	struct kunit *test = kunit_get_current_test();
	struct mctp_test_sock_local_output_config *cfg = test->priv;

	KUNIT_EXPECT_PTR_EQ(test, dst->dev, cfg->dev->mdev);
	KUNIT_EXPECT_EQ(test, dst->halen, cfg->halen);
	KUNIT_EXPECT_MEMEQ(test, dst->haddr, cfg->haddr, dst->halen);

	cfg->invoked = true;

	kfree_skb(skb);

	return cfg->rc;
}

static void mctp_test_sock_sendmsg_extaddr(struct kunit *test)
{
	struct sockaddr_mctp_ext addr = {
		.smctp_base = {
			.smctp_family = AF_MCTP,
			.smctp_tag = MCTP_TAG_OWNER,
			.smctp_network = MCTP_NET_ANY,
		},
	};
	struct mctp_test_sock_local_output_config cfg = { 0 };
	u8 haddr[] = { 0xaa, 0x01 };
	u8 buf[4] = { 0, 1, 2, 3 };
	struct mctp_test_route *rt;
	struct msghdr msg = { 0 };
	struct mctp_test_dev *dev;
	struct mctp_sock *msk;
	struct socket *sock;
	ssize_t send_len;
	struct kvec vec = {
		.iov_base = buf,
		.iov_len = sizeof(buf),
	};

	__mctp_sock_test_init(test, &dev, &rt, &sock);

	/* Expect to see the dst configured up with the addressing data we
	 * provide in the struct sockaddr_mctp_ext
	 */
	cfg.dev = dev;
	cfg.halen = sizeof(haddr);
	memcpy(cfg.haddr, haddr, sizeof(haddr));

	test->priv = &cfg;

	kunit_activate_static_stub(test, mctp_local_output,
				   mctp_test_sock_local_output);

	/* enable and configure direct addressing */
	msk = container_of(sock->sk, struct mctp_sock, sk);
	msk->addr_ext = true;

	addr.smctp_ifindex = dev->ndev->ifindex;
	addr.smctp_halen = sizeof(haddr);
	memcpy(addr.smctp_haddr, haddr, sizeof(haddr));

	msg.msg_name = &addr;
	msg.msg_namelen = sizeof(addr);

	iov_iter_kvec(&msg.msg_iter, ITER_SOURCE, &vec, 1, sizeof(buf));
	send_len = mctp_sendmsg(sock, &msg, sizeof(buf));
	KUNIT_EXPECT_EQ(test, send_len, sizeof(buf));
	KUNIT_EXPECT_TRUE(test, cfg.invoked);

	__mctp_sock_test_fini(test, dev, rt, sock);
}

static void mctp_test_sock_recvmsg_extaddr(struct kunit *test)
{
	struct sockaddr_mctp_ext recv_addr = { 0 };
	u8 rcv_buf[1], rcv_data[] = { 0, 1 };
	u8 haddr[] = { 0xaa, 0x02 };
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct mctp_skb_cb *cb;
	struct mctp_sock *msk;
	struct sk_buff *skb;
	struct mctp_hdr hdr;
	struct socket *sock;
	struct msghdr msg;
	ssize_t recv_len;
	int rc;
	struct kvec vec = {
		.iov_base = rcv_buf,
		.iov_len = sizeof(rcv_buf),
	};

	__mctp_sock_test_init(test, &dev, &rt, &sock);

	/* enable extended addressing on recv */
	msk = container_of(sock->sk, struct mctp_sock, sk);
	msk->addr_ext = true;

	/* base incoming header, using a nul-EID dest */
	hdr.ver = 1;
	hdr.dest = 0;
	hdr.src = 9;
	hdr.flags_seq_tag = MCTP_HDR_FLAG_SOM | MCTP_HDR_FLAG_EOM |
			    MCTP_HDR_FLAG_TO;

	skb = mctp_test_create_skb_data(&hdr, &rcv_data);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	mctp_test_skb_set_dev(skb, dev);

	/* set incoming extended address data */
	cb = mctp_cb(skb);
	cb->halen = sizeof(haddr);
	cb->ifindex = dev->ndev->ifindex;
	memcpy(cb->haddr, haddr, sizeof(haddr));

	/* Deliver to socket. The route input path pulls the network header,
	 * leaving skb data at type byte onwards. recvmsg will consume the
	 * type for addr.smctp_type
	 */
	skb_pull(skb, sizeof(hdr));
	rc = sock_queue_rcv_skb(sock->sk, skb);
	KUNIT_ASSERT_EQ(test, rc, 0);

	msg.msg_name = &recv_addr;
	msg.msg_namelen = sizeof(recv_addr);
	iov_iter_kvec(&msg.msg_iter, ITER_DEST, &vec, 1, sizeof(rcv_buf));

	recv_len = mctp_recvmsg(sock, &msg, sizeof(rcv_buf),
				MSG_DONTWAIT | MSG_TRUNC);

	KUNIT_EXPECT_EQ(test, recv_len, sizeof(rcv_buf));

	/* expect our extended address to be populated from hdr and cb */
	KUNIT_EXPECT_EQ(test, msg.msg_namelen, sizeof(recv_addr));
	KUNIT_EXPECT_EQ(test, recv_addr.smctp_base.smctp_family, AF_MCTP);
	KUNIT_EXPECT_EQ(test, recv_addr.smctp_ifindex, dev->ndev->ifindex);
	KUNIT_EXPECT_EQ(test, recv_addr.smctp_halen, sizeof(haddr));
	KUNIT_EXPECT_MEMEQ(test, recv_addr.smctp_haddr, haddr, sizeof(haddr));

	__mctp_sock_test_fini(test, dev, rt, sock);
}

static const struct mctp_test_bind_setup bind_addrany_netdefault_type1 = {
	.bind_addr = MCTP_ADDR_ANY, .bind_net = MCTP_NET_ANY, .bind_type = 1,
};

static const struct mctp_test_bind_setup bind_addrany_net2_type1 = {
	.bind_addr = MCTP_ADDR_ANY, .bind_net = 2, .bind_type = 1,
};

/* 1 is default net */
static const struct mctp_test_bind_setup bind_addr8_net1_type1 = {
	.bind_addr = 8, .bind_net = 1, .bind_type = 1,
};

static const struct mctp_test_bind_setup bind_addrany_net1_type1 = {
	.bind_addr = MCTP_ADDR_ANY, .bind_net = 1, .bind_type = 1,
};

/* 2 is an arbitrary net */
static const struct mctp_test_bind_setup bind_addr8_net2_type1 = {
	.bind_addr = 8, .bind_net = 2, .bind_type = 1,
};

static const struct mctp_test_bind_setup bind_addr8_netdefault_type1 = {
	.bind_addr = 8, .bind_net = MCTP_NET_ANY, .bind_type = 1,
};

static const struct mctp_test_bind_setup bind_addrany_net2_type2 = {
	.bind_addr = MCTP_ADDR_ANY, .bind_net = 2, .bind_type = 2,
};

static const struct mctp_test_bind_setup bind_addrany_net2_type1_peer9 = {
	.bind_addr = MCTP_ADDR_ANY, .bind_net = 2, .bind_type = 1,
	.have_peer = true, .peer_addr = 9, .peer_net = 2,
};

struct mctp_bind_pair_test {
	const struct mctp_test_bind_setup *bind1;
	const struct mctp_test_bind_setup *bind2;
	int error;
};

/* Pairs of binds and whether they will conflict */
static const struct mctp_bind_pair_test mctp_bind_pair_tests[] = {
	/* Both ADDR_ANY, conflict */
	{ &bind_addrany_netdefault_type1, &bind_addrany_netdefault_type1,
	  EADDRINUSE },
	/* Same specific EID, conflict */
	{ &bind_addr8_netdefault_type1, &bind_addr8_netdefault_type1,
	  EADDRINUSE },
	/* ADDR_ANY vs specific EID, OK */
	{ &bind_addrany_netdefault_type1, &bind_addr8_netdefault_type1, 0 },
	/* ADDR_ANY different types, OK */
	{ &bind_addrany_net2_type2, &bind_addrany_net2_type1, 0 },
	/* ADDR_ANY different nets, OK */
	{ &bind_addrany_net2_type1, &bind_addrany_netdefault_type1, 0 },

	/* specific EID, NET_ANY (resolves to default)
	 *  vs specific EID, explicit default net 1, conflict
	 */
	{ &bind_addr8_netdefault_type1, &bind_addr8_net1_type1, EADDRINUSE },

	/* specific EID, net 1 vs specific EID, net 2, ok */
	{ &bind_addr8_net1_type1, &bind_addr8_net2_type1, 0 },

	/* ANY_ADDR, NET_ANY (doesn't resolve to default)
	 *  vs ADDR_ANY, explicit default net 1, OK
	 */
	{ &bind_addrany_netdefault_type1, &bind_addrany_net1_type1, 0 },

	/* specific remote peer doesn't conflict with any-peer bind */
	{ &bind_addrany_net2_type1_peer9, &bind_addrany_net2_type1, 0 },

	/* bind() NET_ANY is allowed with a connect() net */
	{ &bind_addrany_net2_type1_peer9, &bind_addrany_netdefault_type1, 0 },
};

static void mctp_bind_pair_desc(const struct mctp_bind_pair_test *t, char *desc)
{
	char peer1[25] = {0}, peer2[25] = {0};

	if (t->bind1->have_peer)
		snprintf(peer1, sizeof(peer1), ", peer %d net %d",
			 t->bind1->peer_addr, t->bind1->peer_net);
	if (t->bind2->have_peer)
		snprintf(peer2, sizeof(peer2), ", peer %d net %d",
			 t->bind2->peer_addr, t->bind2->peer_net);

	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "{bind(addr %d, type %d, net %d%s)} {bind(addr %d, type %d, net %d%s)} -> error %d",
		 t->bind1->bind_addr, t->bind1->bind_type,
		 t->bind1->bind_net, peer1,
		 t->bind2->bind_addr, t->bind2->bind_type,
		 t->bind2->bind_net, peer2, t->error);
}

KUNIT_ARRAY_PARAM(mctp_bind_pair, mctp_bind_pair_tests, mctp_bind_pair_desc);

static void mctp_test_bind_invalid(struct kunit *test)
{
	struct socket *sock;
	int rc;

	/* bind() fails if the bind() vs connect() networks mismatch. */
	const struct mctp_test_bind_setup bind_connect_net_mismatch = {
		.bind_addr = MCTP_ADDR_ANY, .bind_net = 1, .bind_type = 1,
		.have_peer = true, .peer_addr = 9, .peer_net = 2,
	};
	mctp_test_bind_run(test, &bind_connect_net_mismatch, &rc, &sock);
	KUNIT_EXPECT_EQ(test, -rc, EINVAL);
	sock_release(sock);
}

static int
mctp_test_bind_conflicts_inner(struct kunit *test,
			       const struct mctp_test_bind_setup *bind1,
			       const struct mctp_test_bind_setup *bind2)
{
	struct socket *sock1 = NULL, *sock2 = NULL, *sock3 = NULL;
	int bind_errno;

	/* Bind to first address, always succeeds */
	mctp_test_bind_run(test, bind1, &bind_errno, &sock1);
	KUNIT_EXPECT_EQ(test, bind_errno, 0);

	/* A second identical bind always fails */
	mctp_test_bind_run(test, bind1, &bind_errno, &sock2);
	KUNIT_EXPECT_EQ(test, -bind_errno, EADDRINUSE);

	/* A different bind, result is returned */
	mctp_test_bind_run(test, bind2, &bind_errno, &sock3);

	if (sock1)
		sock_release(sock1);
	if (sock2)
		sock_release(sock2);
	if (sock3)
		sock_release(sock3);

	return bind_errno;
}

static void mctp_test_bind_conflicts(struct kunit *test)
{
	const struct mctp_bind_pair_test *pair;
	int bind_errno;

	pair = test->param_value;

	bind_errno =
		mctp_test_bind_conflicts_inner(test, pair->bind1, pair->bind2);
	KUNIT_EXPECT_EQ(test, -bind_errno, pair->error);

	/* swapping the calls, the second bind should still fail */
	bind_errno =
		mctp_test_bind_conflicts_inner(test, pair->bind2, pair->bind1);
	KUNIT_EXPECT_EQ(test, -bind_errno, pair->error);
}

static void mctp_test_assumptions(struct kunit *test)
{
	/* check assumption of default net from bind_addr8_net1_type1 */
	KUNIT_ASSERT_EQ(test, mctp_default_net(&init_net), 1);
}

static struct kunit_case mctp_test_cases[] = {
	KUNIT_CASE(mctp_test_assumptions),
	KUNIT_CASE(mctp_test_sock_sendmsg_extaddr),
	KUNIT_CASE(mctp_test_sock_recvmsg_extaddr),
	KUNIT_CASE_PARAM(mctp_test_bind_conflicts, mctp_bind_pair_gen_params),
	KUNIT_CASE(mctp_test_bind_invalid),
	{}
};

static struct kunit_suite mctp_test_suite = {
	.name = "mctp-sock",
	.test_cases = mctp_test_cases,
};

kunit_test_suite(mctp_test_suite);
