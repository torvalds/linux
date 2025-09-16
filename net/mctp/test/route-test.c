// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

/* keep clangd happy when compiled outside of the route.c include */
#include <net/mctp.h>
#include <net/mctpdevice.h>

#include "utils.h"

#define mctp_test_create_skb_data(h, d) \
	__mctp_test_create_skb_data(h, d, sizeof(*d))

struct mctp_frag_test {
	unsigned int mtu;
	unsigned int msgsize;
	unsigned int n_frags;
};

static void mctp_test_fragment(struct kunit *test)
{
	const struct mctp_frag_test *params;
	struct mctp_test_pktqueue tpq;
	int rc, i, n, mtu, msgsize;
	struct mctp_test_dev *dev;
	struct mctp_dst dst;
	struct sk_buff *skb;
	struct mctp_hdr hdr;
	u8 seq;

	params = test->param_value;
	mtu = params->mtu;
	msgsize = params->msgsize;

	hdr.ver = 1;
	hdr.src = 8;
	hdr.dest = 10;
	hdr.flags_seq_tag = MCTP_HDR_FLAG_TO;

	skb = mctp_test_create_skb(&hdr, msgsize);
	KUNIT_ASSERT_TRUE(test, skb);

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	mctp_test_dst_setup(test, &dst, dev, &tpq, mtu);

	rc = mctp_do_fragment_route(&dst, skb, mtu, MCTP_TAG_OWNER);
	KUNIT_EXPECT_FALSE(test, rc);

	n = tpq.pkts.qlen;

	KUNIT_EXPECT_EQ(test, n, params->n_frags);

	for (i = 0;; i++) {
		struct mctp_hdr *hdr2;
		struct sk_buff *skb2;
		u8 tag_mask, seq2;
		bool first, last;

		first = i == 0;
		last = i == (n - 1);

		skb2 = skb_dequeue(&tpq.pkts);

		if (!skb2)
			break;

		hdr2 = mctp_hdr(skb2);

		tag_mask = MCTP_HDR_TAG_MASK | MCTP_HDR_FLAG_TO;

		KUNIT_EXPECT_EQ(test, hdr2->ver, hdr.ver);
		KUNIT_EXPECT_EQ(test, hdr2->src, hdr.src);
		KUNIT_EXPECT_EQ(test, hdr2->dest, hdr.dest);
		KUNIT_EXPECT_EQ(test, hdr2->flags_seq_tag & tag_mask,
				hdr.flags_seq_tag & tag_mask);

		KUNIT_EXPECT_EQ(test,
				!!(hdr2->flags_seq_tag & MCTP_HDR_FLAG_SOM), first);
		KUNIT_EXPECT_EQ(test,
				!!(hdr2->flags_seq_tag & MCTP_HDR_FLAG_EOM), last);

		seq2 = (hdr2->flags_seq_tag >> MCTP_HDR_SEQ_SHIFT) &
			MCTP_HDR_SEQ_MASK;

		if (first) {
			seq = seq2;
		} else {
			seq++;
			KUNIT_EXPECT_EQ(test, seq2, seq & MCTP_HDR_SEQ_MASK);
		}

		if (!last)
			KUNIT_EXPECT_EQ(test, skb2->len, mtu);
		else
			KUNIT_EXPECT_LE(test, skb2->len, mtu);

		kfree_skb(skb2);
	}

	mctp_test_dst_release(&dst, &tpq);
	mctp_test_destroy_dev(dev);
}

static const struct mctp_frag_test mctp_frag_tests[] = {
	{.mtu = 68, .msgsize = 63, .n_frags = 1},
	{.mtu = 68, .msgsize = 64, .n_frags = 1},
	{.mtu = 68, .msgsize = 65, .n_frags = 2},
	{.mtu = 68, .msgsize = 66, .n_frags = 2},
	{.mtu = 68, .msgsize = 127, .n_frags = 2},
	{.mtu = 68, .msgsize = 128, .n_frags = 2},
	{.mtu = 68, .msgsize = 129, .n_frags = 3},
	{.mtu = 68, .msgsize = 130, .n_frags = 3},
};

static void mctp_frag_test_to_desc(const struct mctp_frag_test *t, char *desc)
{
	sprintf(desc, "mtu %d len %d -> %d frags",
		t->msgsize, t->mtu, t->n_frags);
}

KUNIT_ARRAY_PARAM(mctp_frag, mctp_frag_tests, mctp_frag_test_to_desc);

struct mctp_rx_input_test {
	struct mctp_hdr hdr;
	bool input;
};

static void mctp_test_rx_input(struct kunit *test)
{
	const struct mctp_rx_input_test *params;
	struct mctp_test_pktqueue tpq;
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct sk_buff *skb;

	params = test->param_value;
	test->priv = &tpq;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	rt = mctp_test_create_route_direct(&init_net, dev->mdev, 8, 68);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

	skb = mctp_test_create_skb(&params->hdr, 1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	mctp_test_pktqueue_init(&tpq);

	mctp_pkttype_receive(skb, dev->ndev, &mctp_packet_type, NULL);

	KUNIT_EXPECT_EQ(test, !!tpq.pkts.qlen, params->input);

	skb_queue_purge(&tpq.pkts);
	mctp_test_route_destroy(test, rt);
	mctp_test_destroy_dev(dev);
}

#define RX_HDR(_ver, _src, _dest, _fst) \
	{ .ver = _ver, .src = _src, .dest = _dest, .flags_seq_tag = _fst }

/* we have a route for EID 8 only */
static const struct mctp_rx_input_test mctp_rx_input_tests[] = {
	{ .hdr = RX_HDR(1, 10, 8, 0), .input = true },
	{ .hdr = RX_HDR(1, 10, 9, 0), .input = false }, /* no input route */
	{ .hdr = RX_HDR(2, 10, 8, 0), .input = false }, /* invalid version */
};

static void mctp_rx_input_test_to_desc(const struct mctp_rx_input_test *t,
				       char *desc)
{
	sprintf(desc, "{%x,%x,%x,%x}", t->hdr.ver, t->hdr.src, t->hdr.dest,
		t->hdr.flags_seq_tag);
}

KUNIT_ARRAY_PARAM(mctp_rx_input, mctp_rx_input_tests,
		  mctp_rx_input_test_to_desc);

/* set up a local dev, route on EID 8, and a socket listening on type 0 */
static void __mctp_route_test_init(struct kunit *test,
				   struct mctp_test_dev **devp,
				   struct mctp_dst *dst,
				   struct mctp_test_pktqueue *tpq,
				   struct socket **sockp,
				   unsigned int netid)
{
	struct sockaddr_mctp addr = {0};
	struct mctp_test_dev *dev;
	struct socket *sock;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	if (netid != MCTP_NET_ANY)
		WRITE_ONCE(dev->mdev->net, netid);

	mctp_test_dst_setup(test, dst, dev, tpq, 68);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	addr.smctp_family = AF_MCTP;
	addr.smctp_network = netid;
	addr.smctp_addr.s_addr = 8;
	addr.smctp_type = 0;
	rc = kernel_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	KUNIT_ASSERT_EQ(test, rc, 0);

	*devp = dev;
	*sockp = sock;
}

static void __mctp_route_test_fini(struct kunit *test,
				   struct mctp_test_dev *dev,
				   struct mctp_dst *dst,
				   struct mctp_test_pktqueue *tpq,
				   struct socket *sock)
{
	sock_release(sock);
	mctp_test_dst_release(dst, tpq);
	mctp_test_destroy_dev(dev);
}

struct mctp_route_input_sk_test {
	struct mctp_hdr hdr;
	u8 type;
	bool deliver;
};

static void mctp_test_route_input_sk(struct kunit *test)
{
	const struct mctp_route_input_sk_test *params;
	struct mctp_test_pktqueue tpq;
	struct sk_buff *skb, *skb2;
	struct mctp_test_dev *dev;
	struct mctp_dst dst;
	struct socket *sock;
	int rc;

	params = test->param_value;

	__mctp_route_test_init(test, &dev, &dst, &tpq, &sock, MCTP_NET_ANY);

	skb = mctp_test_create_skb_data(&params->hdr, &params->type);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	mctp_test_skb_set_dev(skb, dev);
	mctp_test_pktqueue_init(&tpq);

	rc = mctp_dst_input(&dst, skb);

	if (params->deliver) {
		KUNIT_EXPECT_EQ(test, rc, 0);

		skb2 = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, skb2);
		KUNIT_EXPECT_EQ(test, skb2->len, 1);

		skb_free_datagram(sock->sk, skb2);

	} else {
		KUNIT_EXPECT_NE(test, rc, 0);
		skb2 = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);
		KUNIT_EXPECT_NULL(test, skb2);
	}

	__mctp_route_test_fini(test, dev, &dst, &tpq, sock);
}

#define FL_S	(MCTP_HDR_FLAG_SOM)
#define FL_E	(MCTP_HDR_FLAG_EOM)
#define FL_TO	(MCTP_HDR_FLAG_TO)
#define FL_T(t)	((t) & MCTP_HDR_TAG_MASK)

static const struct mctp_route_input_sk_test mctp_route_input_sk_tests[] = {
	{ .hdr = RX_HDR(1, 10, 8, FL_S | FL_E | FL_TO), .type = 0, .deliver = true },
	{ .hdr = RX_HDR(1, 10, 8, FL_S | FL_E | FL_TO), .type = 1, .deliver = false },
	{ .hdr = RX_HDR(1, 10, 8, FL_S | FL_E), .type = 0, .deliver = false },
	{ .hdr = RX_HDR(1, 10, 8, FL_E | FL_TO), .type = 0, .deliver = false },
	{ .hdr = RX_HDR(1, 10, 8, FL_TO), .type = 0, .deliver = false },
	{ .hdr = RX_HDR(1, 10, 8, 0), .type = 0, .deliver = false },
};

static void mctp_route_input_sk_to_desc(const struct mctp_route_input_sk_test *t,
					char *desc)
{
	sprintf(desc, "{%x,%x,%x,%x} type %d", t->hdr.ver, t->hdr.src,
		t->hdr.dest, t->hdr.flags_seq_tag, t->type);
}

KUNIT_ARRAY_PARAM(mctp_route_input_sk, mctp_route_input_sk_tests,
		  mctp_route_input_sk_to_desc);

struct mctp_route_input_sk_reasm_test {
	const char *name;
	struct mctp_hdr hdrs[4];
	int n_hdrs;
	int rx_len;
};

static void mctp_test_route_input_sk_reasm(struct kunit *test)
{
	const struct mctp_route_input_sk_reasm_test *params;
	struct mctp_test_pktqueue tpq;
	struct sk_buff *skb, *skb2;
	struct mctp_test_dev *dev;
	struct mctp_dst dst;
	struct socket *sock;
	int i, rc;
	u8 c;

	params = test->param_value;

	__mctp_route_test_init(test, &dev, &dst, &tpq, &sock, MCTP_NET_ANY);

	for (i = 0; i < params->n_hdrs; i++) {
		c = i;
		skb = mctp_test_create_skb_data(&params->hdrs[i], &c);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

		mctp_test_skb_set_dev(skb, dev);

		rc = mctp_dst_input(&dst, skb);
	}

	skb2 = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);

	if (params->rx_len) {
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, skb2);
		KUNIT_EXPECT_EQ(test, skb2->len, params->rx_len);
		skb_free_datagram(sock->sk, skb2);

	} else {
		KUNIT_EXPECT_NULL(test, skb2);
	}

	__mctp_route_test_fini(test, dev, &dst, &tpq, sock);
}

#define RX_FRAG(f, s) RX_HDR(1, 10, 8, FL_TO | (f) | ((s) << MCTP_HDR_SEQ_SHIFT))

static const struct mctp_route_input_sk_reasm_test mctp_route_input_sk_reasm_tests[] = {
	{
		.name = "single packet",
		.hdrs = {
			RX_FRAG(FL_S | FL_E, 0),
		},
		.n_hdrs = 1,
		.rx_len = 1,
	},
	{
		.name = "single packet, offset seq",
		.hdrs = {
			RX_FRAG(FL_S | FL_E, 1),
		},
		.n_hdrs = 1,
		.rx_len = 1,
	},
	{
		.name = "start & end packets",
		.hdrs = {
			RX_FRAG(FL_S, 0),
			RX_FRAG(FL_E, 1),
		},
		.n_hdrs = 2,
		.rx_len = 2,
	},
	{
		.name = "start & end packets, offset seq",
		.hdrs = {
			RX_FRAG(FL_S, 1),
			RX_FRAG(FL_E, 2),
		},
		.n_hdrs = 2,
		.rx_len = 2,
	},
	{
		.name = "start & end packets, out of order",
		.hdrs = {
			RX_FRAG(FL_E, 1),
			RX_FRAG(FL_S, 0),
		},
		.n_hdrs = 2,
		.rx_len = 0,
	},
	{
		.name = "start, middle & end packets",
		.hdrs = {
			RX_FRAG(FL_S, 0),
			RX_FRAG(0,    1),
			RX_FRAG(FL_E, 2),
		},
		.n_hdrs = 3,
		.rx_len = 3,
	},
	{
		.name = "missing seq",
		.hdrs = {
			RX_FRAG(FL_S, 0),
			RX_FRAG(FL_E, 2),
		},
		.n_hdrs = 2,
		.rx_len = 0,
	},
	{
		.name = "seq wrap",
		.hdrs = {
			RX_FRAG(FL_S, 3),
			RX_FRAG(FL_E, 0),
		},
		.n_hdrs = 2,
		.rx_len = 2,
	},
};

static void mctp_route_input_sk_reasm_to_desc(
				const struct mctp_route_input_sk_reasm_test *t,
				char *desc)
{
	sprintf(desc, "%s", t->name);
}

KUNIT_ARRAY_PARAM(mctp_route_input_sk_reasm, mctp_route_input_sk_reasm_tests,
		  mctp_route_input_sk_reasm_to_desc);

struct mctp_route_input_sk_keys_test {
	const char	*name;
	mctp_eid_t	key_peer_addr;
	mctp_eid_t	key_local_addr;
	u8		key_tag;
	struct mctp_hdr hdr;
	bool		deliver;
};

/* test packet rx in the presence of various key configurations */
static void mctp_test_route_input_sk_keys(struct kunit *test)
{
	const struct mctp_route_input_sk_keys_test *params;
	struct mctp_test_pktqueue tpq;
	struct sk_buff *skb, *skb2;
	struct mctp_test_dev *dev;
	struct mctp_sk_key *key;
	struct netns_mctp *mns;
	struct mctp_sock *msk;
	struct socket *sock;
	unsigned long flags;
	struct mctp_dst dst;
	unsigned int net;
	int rc;
	u8 c;

	params = test->param_value;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	net = READ_ONCE(dev->mdev->net);

	mctp_test_dst_setup(test, &dst, dev, &tpq, 68);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	msk = container_of(sock->sk, struct mctp_sock, sk);
	mns = &sock_net(sock->sk)->mctp;

	/* set the incoming tag according to test params */
	key = mctp_key_alloc(msk, net, params->key_local_addr,
			     params->key_peer_addr, params->key_tag,
			     GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, key);

	spin_lock_irqsave(&mns->keys_lock, flags);
	mctp_reserve_tag(&init_net, key, msk);
	spin_unlock_irqrestore(&mns->keys_lock, flags);

	/* create packet and route */
	c = 0;
	skb = mctp_test_create_skb_data(&params->hdr, &c);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	mctp_test_skb_set_dev(skb, dev);

	rc = mctp_dst_input(&dst, skb);

	/* (potentially) receive message */
	skb2 = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);

	if (params->deliver)
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, skb2);
	else
		KUNIT_EXPECT_PTR_EQ(test, skb2, NULL);

	if (skb2)
		skb_free_datagram(sock->sk, skb2);

	mctp_key_unref(key);
	__mctp_route_test_fini(test, dev, &dst, &tpq, sock);
}

static const struct mctp_route_input_sk_keys_test mctp_route_input_sk_keys_tests[] = {
	{
		.name = "direct match",
		.key_peer_addr = 9,
		.key_local_addr = 8,
		.key_tag = 1,
		.hdr = RX_HDR(1, 9, 8, FL_S | FL_E | FL_T(1)),
		.deliver = true,
	},
	{
		.name = "flipped src/dest",
		.key_peer_addr = 8,
		.key_local_addr = 9,
		.key_tag = 1,
		.hdr = RX_HDR(1, 9, 8, FL_S | FL_E | FL_T(1)),
		.deliver = false,
	},
	{
		.name = "peer addr mismatch",
		.key_peer_addr = 9,
		.key_local_addr = 8,
		.key_tag = 1,
		.hdr = RX_HDR(1, 10, 8, FL_S | FL_E | FL_T(1)),
		.deliver = false,
	},
	{
		.name = "tag value mismatch",
		.key_peer_addr = 9,
		.key_local_addr = 8,
		.key_tag = 1,
		.hdr = RX_HDR(1, 9, 8, FL_S | FL_E | FL_T(2)),
		.deliver = false,
	},
	{
		.name = "TO mismatch",
		.key_peer_addr = 9,
		.key_local_addr = 8,
		.key_tag = 1,
		.hdr = RX_HDR(1, 9, 8, FL_S | FL_E | FL_T(1) | FL_TO),
		.deliver = false,
	},
	{
		.name = "broadcast response",
		.key_peer_addr = MCTP_ADDR_ANY,
		.key_local_addr = 8,
		.key_tag = 1,
		.hdr = RX_HDR(1, 11, 8, FL_S | FL_E | FL_T(1)),
		.deliver = true,
	},
	{
		.name = "any local match",
		.key_peer_addr = 12,
		.key_local_addr = MCTP_ADDR_ANY,
		.key_tag = 1,
		.hdr = RX_HDR(1, 12, 8, FL_S | FL_E | FL_T(1)),
		.deliver = true,
	},
};

static void mctp_route_input_sk_keys_to_desc(
				const struct mctp_route_input_sk_keys_test *t,
				char *desc)
{
	sprintf(desc, "%s", t->name);
}

KUNIT_ARRAY_PARAM(mctp_route_input_sk_keys, mctp_route_input_sk_keys_tests,
		  mctp_route_input_sk_keys_to_desc);

struct test_net {
	unsigned int netid;
	struct mctp_test_dev *dev;
	struct mctp_test_pktqueue tpq;
	struct mctp_dst dst;
	struct socket *sock;
	struct sk_buff *skb;
	struct mctp_sk_key *key;
	struct {
		u8 type;
		unsigned int data;
	} msg;
};

static void
mctp_test_route_input_multiple_nets_bind_init(struct kunit *test,
					      struct test_net *t)
{
	struct mctp_hdr hdr = RX_HDR(1, 9, 8, FL_S | FL_E | FL_T(1) | FL_TO);

	t->msg.data = t->netid;

	__mctp_route_test_init(test, &t->dev, &t->dst, &t->tpq, &t->sock,
			       t->netid);

	t->skb = mctp_test_create_skb_data(&hdr, &t->msg);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, t->skb);
	mctp_test_skb_set_dev(t->skb, t->dev);
	mctp_test_pktqueue_init(&t->tpq);
}

static void
mctp_test_route_input_multiple_nets_bind_fini(struct kunit *test,
					      struct test_net *t)
{
	__mctp_route_test_fini(test, t->dev, &t->dst, &t->tpq, t->sock);
}

/* Test that skbs from different nets (otherwise identical) get routed to their
 * corresponding socket via the sockets' bind()
 */
static void mctp_test_route_input_multiple_nets_bind(struct kunit *test)
{
	struct sk_buff *rx_skb1, *rx_skb2;
	struct test_net t1, t2;
	int rc;

	t1.netid = 1;
	t2.netid = 2;

	t1.msg.type = 0;
	t2.msg.type = 0;

	mctp_test_route_input_multiple_nets_bind_init(test, &t1);
	mctp_test_route_input_multiple_nets_bind_init(test, &t2);

	rc = mctp_dst_input(&t1.dst, t1.skb);
	KUNIT_ASSERT_EQ(test, rc, 0);
	rc = mctp_dst_input(&t2.dst, t2.skb);
	KUNIT_ASSERT_EQ(test, rc, 0);

	rx_skb1 = skb_recv_datagram(t1.sock->sk, MSG_DONTWAIT, &rc);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, rx_skb1);
	KUNIT_EXPECT_EQ(test, rx_skb1->len, sizeof(t1.msg));
	KUNIT_EXPECT_EQ(test,
			*(unsigned int *)skb_pull(rx_skb1, sizeof(t1.msg.data)),
			t1.netid);
	kfree_skb(rx_skb1);

	rx_skb2 = skb_recv_datagram(t2.sock->sk, MSG_DONTWAIT, &rc);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, rx_skb2);
	KUNIT_EXPECT_EQ(test, rx_skb2->len, sizeof(t2.msg));
	KUNIT_EXPECT_EQ(test,
			*(unsigned int *)skb_pull(rx_skb2, sizeof(t2.msg.data)),
			t2.netid);
	kfree_skb(rx_skb2);

	mctp_test_route_input_multiple_nets_bind_fini(test, &t1);
	mctp_test_route_input_multiple_nets_bind_fini(test, &t2);
}

static void
mctp_test_route_input_multiple_nets_key_init(struct kunit *test,
					     struct test_net *t)
{
	struct mctp_hdr hdr = RX_HDR(1, 9, 8, FL_S | FL_E | FL_T(1));
	struct mctp_sock *msk;
	struct netns_mctp *mns;
	unsigned long flags;

	t->msg.data = t->netid;

	__mctp_route_test_init(test, &t->dev, &t->dst, &t->tpq, &t->sock,
			       t->netid);

	msk = container_of(t->sock->sk, struct mctp_sock, sk);

	t->key = mctp_key_alloc(msk, t->netid, hdr.dest, hdr.src, 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, t->key);

	mns = &sock_net(t->sock->sk)->mctp;
	spin_lock_irqsave(&mns->keys_lock, flags);
	mctp_reserve_tag(&init_net, t->key, msk);
	spin_unlock_irqrestore(&mns->keys_lock, flags);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, t->key);
	t->skb = mctp_test_create_skb_data(&hdr, &t->msg);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, t->skb);
	mctp_test_skb_set_dev(t->skb, t->dev);
}

static void
mctp_test_route_input_multiple_nets_key_fini(struct kunit *test,
					     struct test_net *t)
{
	mctp_key_unref(t->key);
	__mctp_route_test_fini(test, t->dev, &t->dst, &t->tpq, t->sock);
}

/* test that skbs from different nets (otherwise identical) get routed to their
 * corresponding socket via the sk_key
 */
static void mctp_test_route_input_multiple_nets_key(struct kunit *test)
{
	struct sk_buff *rx_skb1, *rx_skb2;
	struct test_net t1, t2;
	int rc;

	t1.netid = 1;
	t2.netid = 2;

	/* use type 1 which is not bound */
	t1.msg.type = 1;
	t2.msg.type = 1;

	mctp_test_route_input_multiple_nets_key_init(test, &t1);
	mctp_test_route_input_multiple_nets_key_init(test, &t2);

	rc = mctp_dst_input(&t1.dst, t1.skb);
	KUNIT_ASSERT_EQ(test, rc, 0);
	rc = mctp_dst_input(&t2.dst, t2.skb);
	KUNIT_ASSERT_EQ(test, rc, 0);

	rx_skb1 = skb_recv_datagram(t1.sock->sk, MSG_DONTWAIT, &rc);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, rx_skb1);
	KUNIT_EXPECT_EQ(test, rx_skb1->len, sizeof(t1.msg));
	KUNIT_EXPECT_EQ(test,
			*(unsigned int *)skb_pull(rx_skb1, sizeof(t1.msg.data)),
			t1.netid);
	kfree_skb(rx_skb1);

	rx_skb2 = skb_recv_datagram(t2.sock->sk, MSG_DONTWAIT, &rc);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, rx_skb2);
	KUNIT_EXPECT_EQ(test, rx_skb2->len, sizeof(t2.msg));
	KUNIT_EXPECT_EQ(test,
			*(unsigned int *)skb_pull(rx_skb2, sizeof(t2.msg.data)),
			t2.netid);
	kfree_skb(rx_skb2);

	mctp_test_route_input_multiple_nets_key_fini(test, &t1);
	mctp_test_route_input_multiple_nets_key_fini(test, &t2);
}

/* Input route to socket, using a single-packet message, where sock delivery
 * fails. Ensure we're handling the failure appropriately.
 */
static void mctp_test_route_input_sk_fail_single(struct kunit *test)
{
	const struct mctp_hdr hdr = RX_HDR(1, 10, 8, FL_S | FL_E | FL_TO);
	struct mctp_test_pktqueue tpq;
	struct mctp_test_dev *dev;
	struct mctp_dst dst;
	struct socket *sock;
	struct sk_buff *skb;
	int rc;

	__mctp_route_test_init(test, &dev, &dst, &tpq, &sock, MCTP_NET_ANY);

	/* No rcvbuf space, so delivery should fail. __sock_set_rcvbuf will
	 * clamp the minimum to SOCK_MIN_RCVBUF, so we open-code this.
	 */
	lock_sock(sock->sk);
	WRITE_ONCE(sock->sk->sk_rcvbuf, 0);
	release_sock(sock->sk);

	skb = mctp_test_create_skb(&hdr, 10);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);
	skb_get(skb);

	mctp_test_skb_set_dev(skb, dev);

	/* do route input, which should fail */
	rc = mctp_dst_input(&dst, skb);
	KUNIT_EXPECT_NE(test, rc, 0);

	/* we should hold the only reference to skb */
	KUNIT_EXPECT_EQ(test, refcount_read(&skb->users), 1);
	kfree_skb(skb);

	__mctp_route_test_fini(test, dev, &dst, &tpq, sock);
}

/* Input route to socket, using a fragmented message, where sock delivery fails.
 */
static void mctp_test_route_input_sk_fail_frag(struct kunit *test)
{
	const struct mctp_hdr hdrs[2] = { RX_FRAG(FL_S, 0), RX_FRAG(FL_E, 1) };
	struct mctp_test_pktqueue tpq;
	struct mctp_test_dev *dev;
	struct sk_buff *skbs[2];
	struct mctp_dst dst;
	struct socket *sock;
	unsigned int i;
	int rc;

	__mctp_route_test_init(test, &dev, &dst, &tpq, &sock, MCTP_NET_ANY);

	lock_sock(sock->sk);
	WRITE_ONCE(sock->sk->sk_rcvbuf, 0);
	release_sock(sock->sk);

	for (i = 0; i < ARRAY_SIZE(skbs); i++) {
		skbs[i] = mctp_test_create_skb(&hdrs[i], 10);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skbs[i]);
		skb_get(skbs[i]);

		mctp_test_skb_set_dev(skbs[i], dev);
	}

	/* first route input should succeed, we're only queueing to the
	 * frag list
	 */
	rc = mctp_dst_input(&dst, skbs[0]);
	KUNIT_EXPECT_EQ(test, rc, 0);

	/* final route input should fail to deliver to the socket */
	rc = mctp_dst_input(&dst, skbs[1]);
	KUNIT_EXPECT_NE(test, rc, 0);

	/* we should hold the only reference to both skbs */
	KUNIT_EXPECT_EQ(test, refcount_read(&skbs[0]->users), 1);
	kfree_skb(skbs[0]);

	KUNIT_EXPECT_EQ(test, refcount_read(&skbs[1]->users), 1);
	kfree_skb(skbs[1]);

	__mctp_route_test_fini(test, dev, &dst, &tpq, sock);
}

/* Input route to socket, using a fragmented message created from clones.
 */
static void mctp_test_route_input_cloned_frag(struct kunit *test)
{
	/* 5 packet fragments, forming 2 complete messages */
	const struct mctp_hdr hdrs[5] = {
		RX_FRAG(FL_S, 0),
		RX_FRAG(0, 1),
		RX_FRAG(FL_E, 2),
		RX_FRAG(FL_S, 0),
		RX_FRAG(FL_E, 1),
	};
	const size_t data_len = 3; /* arbitrary */
	u8 compare[3 * ARRAY_SIZE(hdrs)];
	u8 flat[3 * ARRAY_SIZE(hdrs)];
	struct mctp_test_pktqueue tpq;
	struct mctp_test_dev *dev;
	struct sk_buff *skb[5];
	struct sk_buff *rx_skb;
	struct mctp_dst dst;
	struct socket *sock;
	size_t total;
	void *p;
	int rc;

	total = data_len + sizeof(struct mctp_hdr);

	__mctp_route_test_init(test, &dev, &dst, &tpq, &sock, MCTP_NET_ANY);

	/* Create a single skb initially with concatenated packets */
	skb[0] = mctp_test_create_skb(&hdrs[0], 5 * total);
	mctp_test_skb_set_dev(skb[0], dev);
	memset(skb[0]->data, 0 * 0x11, skb[0]->len);
	memcpy(skb[0]->data, &hdrs[0], sizeof(struct mctp_hdr));

	/* Extract and populate packets */
	for (int i = 1; i < 5; i++) {
		skb[i] = skb_clone(skb[i - 1], GFP_ATOMIC);
		KUNIT_ASSERT_TRUE(test, skb[i]);
		p = skb_pull(skb[i], total);
		KUNIT_ASSERT_TRUE(test, p);
		skb_reset_network_header(skb[i]);
		memcpy(skb[i]->data, &hdrs[i], sizeof(struct mctp_hdr));
		memset(&skb[i]->data[sizeof(struct mctp_hdr)], i * 0x11, data_len);
	}
	for (int i = 0; i < 5; i++)
		skb_trim(skb[i], total);

	/* SOM packets have a type byte to match the socket */
	skb[0]->data[4] = 0;
	skb[3]->data[4] = 0;

	skb_dump("pkt1 ", skb[0], false);
	skb_dump("pkt2 ", skb[1], false);
	skb_dump("pkt3 ", skb[2], false);
	skb_dump("pkt4 ", skb[3], false);
	skb_dump("pkt5 ", skb[4], false);

	for (int i = 0; i < 5; i++) {
		KUNIT_EXPECT_EQ(test, refcount_read(&skb[i]->users), 1);
		/* Take a reference so we can check refcounts at the end */
		skb_get(skb[i]);
	}

	/* Feed the fragments into MCTP core */
	for (int i = 0; i < 5; i++) {
		rc = mctp_dst_input(&dst, skb[i]);
		KUNIT_EXPECT_EQ(test, rc, 0);
	}

	/* Receive first reassembled message */
	rx_skb = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_EQ(test, rx_skb->len, 3 * data_len);
	rc = skb_copy_bits(rx_skb, 0, flat, rx_skb->len);
	for (int i = 0; i < rx_skb->len; i++)
		compare[i] = (i / data_len) * 0x11;
	/* Set type byte */
	compare[0] = 0;

	KUNIT_EXPECT_MEMEQ(test, flat, compare, rx_skb->len);
	KUNIT_EXPECT_EQ(test, refcount_read(&rx_skb->users), 1);
	kfree_skb(rx_skb);

	/* Receive second reassembled message */
	rx_skb = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_EQ(test, rx_skb->len, 2 * data_len);
	rc = skb_copy_bits(rx_skb, 0, flat, rx_skb->len);
	for (int i = 0; i < rx_skb->len; i++)
		compare[i] = (i / data_len + 3) * 0x11;
	/* Set type byte */
	compare[0] = 0;

	KUNIT_EXPECT_MEMEQ(test, flat, compare, rx_skb->len);
	KUNIT_EXPECT_EQ(test, refcount_read(&rx_skb->users), 1);
	kfree_skb(rx_skb);

	/* Check input skb refcounts */
	for (int i = 0; i < 5; i++) {
		KUNIT_EXPECT_EQ(test, refcount_read(&skb[i]->users), 1);
		kfree_skb(skb[i]);
	}

	__mctp_route_test_fini(test, dev, &dst, &tpq, sock);
}

#if IS_ENABLED(CONFIG_MCTP_FLOWS)

static void mctp_test_flow_init(struct kunit *test,
				struct mctp_test_dev **devp,
				struct mctp_dst *dst,
				struct mctp_test_pktqueue *tpq,
				struct socket **sock,
				struct sk_buff **skbp,
				unsigned int len)
{
	struct mctp_test_dev *dev;
	struct sk_buff *skb;

	/* we have a slightly odd routing setup here; the test route
	 * is for EID 8, which is our local EID. We don't do a routing
	 * lookup, so that's fine - all we require is a path through
	 * mctp_local_output, which will call dst->output on whatever
	 * route we provide
	 */
	__mctp_route_test_init(test, &dev, dst, tpq, sock, MCTP_NET_ANY);

	/* Assign a single EID. ->addrs is freed on mctp netdev release */
	dev->mdev->addrs = kmalloc(sizeof(u8), GFP_KERNEL);
	dev->mdev->num_addrs = 1;
	dev->mdev->addrs[0] = 8;

	skb = alloc_skb(len + sizeof(struct mctp_hdr) + 1, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, skb);
	__mctp_cb(skb);
	skb_reserve(skb, sizeof(struct mctp_hdr) + 1);
	memset(skb_put(skb, len), 0, len);


	*devp = dev;
	*skbp = skb;
}

static void mctp_test_flow_fini(struct kunit *test,
				struct mctp_test_dev *dev,
				struct mctp_dst *dst,
				struct mctp_test_pktqueue *tpq,
				struct socket *sock)
{
	__mctp_route_test_fini(test, dev, dst, tpq, sock);
}

/* test that an outgoing skb has the correct MCTP extension data set */
static void mctp_test_packet_flow(struct kunit *test)
{
	struct mctp_test_pktqueue tpq;
	struct sk_buff *skb, *skb2;
	struct mctp_test_dev *dev;
	struct mctp_dst dst;
	struct mctp_flow *flow;
	struct socket *sock;
	u8 dst_eid = 8;
	int n, rc;

	mctp_test_flow_init(test, &dev, &dst, &tpq, &sock, &skb, 30);

	rc = mctp_local_output(sock->sk, &dst, skb, dst_eid, MCTP_TAG_OWNER);
	KUNIT_ASSERT_EQ(test, rc, 0);

	n = tpq.pkts.qlen;
	KUNIT_ASSERT_EQ(test, n, 1);

	skb2 = skb_dequeue(&tpq.pkts);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb2);

	flow = skb_ext_find(skb2, SKB_EXT_MCTP);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, flow);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, flow->key);
	KUNIT_ASSERT_PTR_EQ(test, flow->key->sk, sock->sk);

	kfree_skb(skb2);
	mctp_test_flow_fini(test, dev, &dst, &tpq, sock);
}

/* test that outgoing skbs, after fragmentation, all have the correct MCTP
 * extension data set.
 */
static void mctp_test_fragment_flow(struct kunit *test)
{
	struct mctp_test_pktqueue tpq;
	struct mctp_flow *flows[2];
	struct sk_buff *tx_skbs[2];
	struct mctp_test_dev *dev;
	struct mctp_dst dst;
	struct sk_buff *skb;
	struct socket *sock;
	u8 dst_eid = 8;
	int n, rc;

	mctp_test_flow_init(test, &dev, &dst, &tpq, &sock, &skb, 100);

	rc = mctp_local_output(sock->sk, &dst, skb, dst_eid, MCTP_TAG_OWNER);
	KUNIT_ASSERT_EQ(test, rc, 0);

	n = tpq.pkts.qlen;
	KUNIT_ASSERT_EQ(test, n, 2);

	/* both resulting packets should have the same flow data */
	tx_skbs[0] = skb_dequeue(&tpq.pkts);
	tx_skbs[1] = skb_dequeue(&tpq.pkts);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, tx_skbs[0]);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, tx_skbs[1]);

	flows[0] = skb_ext_find(tx_skbs[0], SKB_EXT_MCTP);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, flows[0]);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, flows[0]->key);
	KUNIT_ASSERT_PTR_EQ(test, flows[0]->key->sk, sock->sk);

	flows[1] = skb_ext_find(tx_skbs[1], SKB_EXT_MCTP);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, flows[1]);
	KUNIT_ASSERT_PTR_EQ(test, flows[1]->key, flows[0]->key);

	kfree_skb(tx_skbs[0]);
	kfree_skb(tx_skbs[1]);
	mctp_test_flow_fini(test, dev, &dst, &tpq, sock);
}

#else
static void mctp_test_packet_flow(struct kunit *test)
{
	kunit_skip(test, "Requires CONFIG_MCTP_FLOWS=y");
}

static void mctp_test_fragment_flow(struct kunit *test)
{
	kunit_skip(test, "Requires CONFIG_MCTP_FLOWS=y");
}
#endif

/* Test that outgoing skbs cause a suitable tag to be created */
static void mctp_test_route_output_key_create(struct kunit *test)
{
	const u8 dst_eid = 26, src_eid = 15;
	struct mctp_test_pktqueue tpq;
	const unsigned int netid = 50;
	struct mctp_test_dev *dev;
	struct mctp_sk_key *key;
	struct netns_mctp *mns;
	unsigned long flags;
	struct socket *sock;
	struct sk_buff *skb;
	struct mctp_dst dst;
	bool empty, single;
	const int len = 2;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	WRITE_ONCE(dev->mdev->net, netid);

	mctp_test_dst_setup(test, &dst, dev, &tpq, 68);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	dev->mdev->addrs = kmalloc(sizeof(u8), GFP_KERNEL);
	dev->mdev->num_addrs = 1;
	dev->mdev->addrs[0] = src_eid;

	skb = alloc_skb(sizeof(struct mctp_hdr) + 1 + len, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, skb);
	__mctp_cb(skb);
	skb_reserve(skb, sizeof(struct mctp_hdr) + 1 + len);
	memset(skb_put(skb, len), 0, len);

	mns = &sock_net(sock->sk)->mctp;

	/* We assume we're starting from an empty keys list, which requires
	 * preceding tests to clean up correctly!
	 */
	spin_lock_irqsave(&mns->keys_lock, flags);
	empty = hlist_empty(&mns->keys);
	spin_unlock_irqrestore(&mns->keys_lock, flags);
	KUNIT_ASSERT_TRUE(test, empty);

	rc = mctp_local_output(sock->sk, &dst, skb, dst_eid, MCTP_TAG_OWNER);
	KUNIT_ASSERT_EQ(test, rc, 0);

	key = NULL;
	single = false;
	spin_lock_irqsave(&mns->keys_lock, flags);
	if (!hlist_empty(&mns->keys)) {
		key = hlist_entry(mns->keys.first, struct mctp_sk_key, hlist);
		single = hlist_is_singular_node(&key->hlist, &mns->keys);
	}
	spin_unlock_irqrestore(&mns->keys_lock, flags);

	KUNIT_ASSERT_NOT_NULL(test, key);
	KUNIT_ASSERT_TRUE(test, single);

	KUNIT_EXPECT_EQ(test, key->net, netid);
	KUNIT_EXPECT_EQ(test, key->local_addr, src_eid);
	KUNIT_EXPECT_EQ(test, key->peer_addr, dst_eid);
	/* key has incoming tag, so inverse of what we sent */
	KUNIT_EXPECT_FALSE(test, key->tag & MCTP_TAG_OWNER);

	sock_release(sock);
	mctp_test_dst_release(&dst, &tpq);
	mctp_test_destroy_dev(dev);
}

static void mctp_test_route_extaddr_input(struct kunit *test)
{
	static const unsigned char haddr[] = { 0xaa, 0x55 };
	struct mctp_test_pktqueue tpq;
	struct mctp_skb_cb *cb, *cb2;
	const unsigned int len = 40;
	struct mctp_test_dev *dev;
	struct sk_buff *skb, *skb2;
	struct mctp_dst dst;
	struct mctp_hdr hdr;
	struct socket *sock;
	int rc;

	hdr.ver = 1;
	hdr.src = 10;
	hdr.dest = 8;
	hdr.flags_seq_tag = FL_S | FL_E | FL_TO;

	__mctp_route_test_init(test, &dev, &dst, &tpq, &sock, MCTP_NET_ANY);

	skb = mctp_test_create_skb(&hdr, len);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	/* set our hardware addressing data */
	cb = mctp_cb(skb);
	memcpy(cb->haddr, haddr, sizeof(haddr));
	cb->halen = sizeof(haddr);

	mctp_test_skb_set_dev(skb, dev);

	rc = mctp_dst_input(&dst, skb);
	KUNIT_ASSERT_EQ(test, rc, 0);

	skb2 = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb2);
	KUNIT_ASSERT_EQ(test, skb2->len, len);

	cb2 = mctp_cb(skb2);

	/* Received SKB should have the hardware addressing as set above.
	 * We're likely to have the same actual cb here (ie., cb == cb2),
	 * but it's the comparison that we care about
	 */
	KUNIT_EXPECT_EQ(test, cb2->halen, sizeof(haddr));
	KUNIT_EXPECT_MEMEQ(test, cb2->haddr, haddr, sizeof(haddr));

	kfree_skb(skb2);
	__mctp_route_test_fini(test, dev, &dst, &tpq, sock);
}

static void mctp_test_route_gw_lookup(struct kunit *test)
{
	struct mctp_test_route *rt1, *rt2;
	struct mctp_dst dst = { 0 };
	struct mctp_test_dev *dev;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	/* 8 (local) -> 10 (gateway) via 9 (direct) */
	rt1 = mctp_test_create_route_direct(&init_net, dev->mdev, 9, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt1);
	rt2 = mctp_test_create_route_gw(&init_net, dev->mdev->net, 10, 9, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt2);

	rc = mctp_route_lookup(&init_net, dev->mdev->net, 10, &dst);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_PTR_EQ(test, dst.dev, dev->mdev);
	KUNIT_EXPECT_EQ(test, dst.mtu, dev->ndev->mtu);
	KUNIT_EXPECT_EQ(test, dst.nexthop, 9);
	KUNIT_EXPECT_EQ(test, dst.halen, 0);

	mctp_dst_release(&dst);

	mctp_test_route_destroy(test, rt2);
	mctp_test_route_destroy(test, rt1);
	mctp_test_destroy_dev(dev);
}

static void mctp_test_route_gw_loop(struct kunit *test)
{
	struct mctp_test_route *rt1, *rt2;
	struct mctp_dst dst = { 0 };
	struct mctp_test_dev *dev;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	/* two routes using each other as the gw */
	rt1 = mctp_test_create_route_gw(&init_net, dev->mdev->net, 9, 10, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt1);
	rt2 = mctp_test_create_route_gw(&init_net, dev->mdev->net, 10, 9, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt2);

	/* this should fail, rather than infinite-loop */
	rc = mctp_route_lookup(&init_net, dev->mdev->net, 10, &dst);
	KUNIT_EXPECT_NE(test, rc, 0);

	mctp_test_route_destroy(test, rt2);
	mctp_test_route_destroy(test, rt1);
	mctp_test_destroy_dev(dev);
}

struct mctp_route_gw_mtu_test {
	/* working away from the local stack */
	unsigned int dev, neigh, gw, dst;
	unsigned int exp;
};

static void mctp_route_gw_mtu_to_desc(const struct mctp_route_gw_mtu_test *t,
				      char *desc)
{
	sprintf(desc, "dev %d, neigh %d, gw %d, dst %d -> %d",
		t->dev, t->neigh, t->gw, t->dst, t->exp);
}

static const struct mctp_route_gw_mtu_test mctp_route_gw_mtu_tests[] = {
	/* no route-specific MTUs */
	{ 68, 0, 0, 0, 68 },
	{ 100, 0, 0, 0, 100 },
	/* one route MTU (smaller than dev mtu), others unrestricted */
	{ 100, 68, 0, 0, 68 },
	{ 100, 0, 68, 0, 68 },
	{ 100, 0, 0, 68, 68 },
	/* smallest applied, regardless of order */
	{ 100, 99, 98, 68, 68 },
	{ 99, 100, 98, 68, 68 },
	{ 98, 99, 100, 68, 68 },
	{ 68, 98, 99, 100, 68 },
};

KUNIT_ARRAY_PARAM(mctp_route_gw_mtu, mctp_route_gw_mtu_tests,
		  mctp_route_gw_mtu_to_desc);

static void mctp_test_route_gw_mtu(struct kunit *test)
{
	const struct mctp_route_gw_mtu_test *mtus = test->param_value;
	struct mctp_test_route *rt1, *rt2, *rt3;
	struct mctp_dst dst = { 0 };
	struct mctp_test_dev *dev;
	struct mctp_dev *mdev;
	unsigned int netid;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	dev->ndev->mtu = mtus->dev;
	mdev = dev->mdev;
	netid = mdev->net;

	/* 8 (local) -> 11 (dst) via 10 (gw) via 9 (neigh) */
	rt1 = mctp_test_create_route_direct(&init_net, mdev, 9, mtus->neigh);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt1);

	rt2 = mctp_test_create_route_gw(&init_net, netid, 10, 9, mtus->gw);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt2);

	rt3 = mctp_test_create_route_gw(&init_net, netid, 11, 10, mtus->dst);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt3);

	rc = mctp_route_lookup(&init_net, dev->mdev->net, 11, &dst);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_EQ(test, dst.mtu, mtus->exp);

	mctp_dst_release(&dst);

	mctp_test_route_destroy(test, rt3);
	mctp_test_route_destroy(test, rt2);
	mctp_test_route_destroy(test, rt1);
	mctp_test_destroy_dev(dev);
}

#define MCTP_TEST_LLADDR_LEN 2
struct mctp_test_llhdr {
	unsigned int magic;
	unsigned char src[MCTP_TEST_LLADDR_LEN];
	unsigned char dst[MCTP_TEST_LLADDR_LEN];
};

static const unsigned int mctp_test_llhdr_magic = 0x5c78339c;

static int test_dev_header_create(struct sk_buff *skb, struct net_device *dev,
				  unsigned short type, const void *daddr,
				  const void *saddr, unsigned int len)
{
	struct kunit *test = current->kunit_test;
	struct mctp_test_llhdr *hdr;

	hdr = skb_push(skb, sizeof(*hdr));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hdr);
	skb_reset_mac_header(skb);

	hdr->magic = mctp_test_llhdr_magic;
	memcpy(&hdr->src, saddr, sizeof(hdr->src));
	memcpy(&hdr->dst, daddr, sizeof(hdr->dst));

	return 0;
}

/* Test the dst_output path for a gateway-routed skb: we should have it
 * lookup the nexthop EID in the neighbour table, and call into
 * header_ops->create to resolve that to a lladdr. Our mock header_ops->create
 * will just set a synthetic link-layer header, which we check after transmit.
 */
static void mctp_test_route_gw_output(struct kunit *test)
{
	const unsigned char haddr_self[MCTP_TEST_LLADDR_LEN] = { 0xaa, 0x03 };
	const unsigned char haddr_peer[MCTP_TEST_LLADDR_LEN] = { 0xaa, 0x02 };
	const struct header_ops ops = {
		.create = test_dev_header_create,
	};
	struct mctp_neigh neigh = { 0 };
	struct mctp_test_llhdr *ll_hdr;
	struct mctp_dst dst = { 0 };
	struct mctp_hdr hdr = { 0 };
	struct mctp_test_dev *dev;
	struct sk_buff *skb;
	unsigned char *buf;
	int i, rc;

	dev = mctp_test_create_dev_lladdr(sizeof(haddr_self), haddr_self);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	dev->ndev->header_ops = &ops;

	dst.dev = dev->mdev;
	__mctp_dev_get(dst.dev->dev);
	dst.mtu = 68;
	dst.nexthop = 9;

	/* simple mctp_neigh_add for the gateway (not dest!) endpoint */
	INIT_LIST_HEAD(&neigh.list);
	neigh.dev = dev->mdev;
	mctp_dev_hold(dev->mdev);
	neigh.eid = 9;
	neigh.source = MCTP_NEIGH_STATIC;
	memcpy(neigh.ha, haddr_peer, sizeof(haddr_peer));
	list_add_rcu(&neigh.list, &init_net.mctp.neighbours);

	hdr.ver = 1;
	hdr.src = 8;
	hdr.dest = 10;
	hdr.flags_seq_tag = FL_S | FL_E | FL_TO;

	/* construct enough for a future link-layer header, the provided
	 * mctp header, and 4 bytes of data
	 */
	skb = alloc_skb(sizeof(*ll_hdr) + sizeof(hdr) + 4, GFP_KERNEL);
	skb->dev = dev->ndev;
	__mctp_cb(skb);

	skb_reserve(skb, sizeof(*ll_hdr));

	memcpy(skb_put(skb, sizeof(hdr)), &hdr, sizeof(hdr));
	buf = skb_put(skb, 4);
	for (i = 0; i < 4; i++)
		buf[i] = i;

	/* extra ref over the dev_xmit */
	skb_get(skb);

	rc = mctp_dst_output(&dst, skb);
	KUNIT_EXPECT_EQ(test, rc, 0);

	mctp_dst_release(&dst);
	list_del_rcu(&neigh.list);
	mctp_dev_put(dev->mdev);

	/* check that we have our header created with the correct neighbour */
	ll_hdr = (void *)skb_mac_header(skb);
	KUNIT_EXPECT_EQ(test, ll_hdr->magic, mctp_test_llhdr_magic);
	KUNIT_EXPECT_MEMEQ(test, ll_hdr->src, haddr_self, sizeof(haddr_self));
	KUNIT_EXPECT_MEMEQ(test, ll_hdr->dst, haddr_peer, sizeof(haddr_peer));
	kfree_skb(skb);
}

struct mctp_bind_lookup_test {
	/* header of incoming message */
	struct mctp_hdr hdr;
	u8 ty;
	/* mctp network of incoming interface (smctp_network) */
	unsigned int net;

	/* expected socket, matches .name in lookup_binds, NULL for dropped */
	const char *expect;
};

/* Single-packet TO-set message */
#define LK(src, dst) RX_HDR(1, (src), (dst), FL_S | FL_E | FL_TO)

/* Input message test cases for bind lookup tests.
 *
 * 10 and 11 are local EIDs.
 * 20 and 21 are remote EIDs.
 */
static const struct mctp_bind_lookup_test mctp_bind_lookup_tests[] = {
	/* both local-eid and remote-eid binds, remote eid is preferenced */
	{ .hdr = LK(20, 10),  .ty = 1, .net = 1, .expect = "remote20" },

	{ .hdr = LK(20, 255), .ty = 1, .net = 1, .expect = "remote20" },
	{ .hdr = LK(20, 0),   .ty = 1, .net = 1, .expect = "remote20" },
	{ .hdr = LK(0, 255),  .ty = 1, .net = 1, .expect = "any" },
	{ .hdr = LK(0, 11),   .ty = 1, .net = 1, .expect = "any" },
	{ .hdr = LK(0, 0),    .ty = 1, .net = 1, .expect = "any" },
	{ .hdr = LK(0, 10),   .ty = 1, .net = 1, .expect = "local10" },
	{ .hdr = LK(21, 10),  .ty = 1, .net = 1, .expect = "local10" },
	{ .hdr = LK(21, 11),  .ty = 1, .net = 1, .expect = "remote21local11" },

	/* both src and dest set to eid=99. unusual, but accepted
	 * by MCTP stack currently.
	 */
	{ .hdr = LK(99, 99),  .ty = 1, .net = 1, .expect = "any" },

	/* unbound smctp_type */
	{ .hdr = LK(20, 10),  .ty = 3, .net = 1, .expect = NULL },

	/* smctp_network tests */

	{ .hdr = LK(0, 0),    .ty = 1, .net = 7, .expect = "any" },
	{ .hdr = LK(21, 10),  .ty = 1, .net = 2, .expect = "any" },

	/* remote EID 20 matches, but MCTP_NET_ANY in "remote20" resolved
	 * to net=1, so lookup doesn't match "remote20"
	 */
	{ .hdr = LK(20, 10),  .ty = 1, .net = 3, .expect = "any" },

	{ .hdr = LK(21, 10),  .ty = 1, .net = 3, .expect = "remote21net3" },
	{ .hdr = LK(21, 10),  .ty = 1, .net = 4, .expect = "remote21net4" },
	{ .hdr = LK(21, 10),  .ty = 1, .net = 5, .expect = "remote21net5" },

	{ .hdr = LK(21, 10),  .ty = 1, .net = 5, .expect = "remote21net5" },

	{ .hdr = LK(99, 10),  .ty = 1, .net = 8, .expect = "local10net8" },

	{ .hdr = LK(99, 10),  .ty = 1, .net = 9, .expect = "anynet9" },
	{ .hdr = LK(0, 0),    .ty = 1, .net = 9, .expect = "anynet9" },
	{ .hdr = LK(99, 99),  .ty = 1, .net = 9, .expect = "anynet9" },
	{ .hdr = LK(20, 10),  .ty = 1, .net = 9, .expect = "anynet9" },
};

/* Binds to create during the lookup tests */
static const struct mctp_test_bind_setup lookup_binds[] = {
	/* any address and net, type 1 */
	{ .name = "any", .bind_addr = MCTP_ADDR_ANY,
		.bind_net = MCTP_NET_ANY, .bind_type = 1, },
	/* local eid 10, net 1 (resolved from MCTP_NET_ANY) */
	{ .name = "local10", .bind_addr = 10,
		.bind_net = MCTP_NET_ANY, .bind_type = 1, },
	/* local eid 10, net 8 */
	{ .name = "local10net8", .bind_addr = 10,
		.bind_net = 8, .bind_type = 1, },
	/* any EID, net 9 */
	{ .name = "anynet9", .bind_addr = MCTP_ADDR_ANY,
		.bind_net = 9, .bind_type = 1, },

	/* remote eid 20, net 1, any local eid */
	{ .name = "remote20", .bind_addr = MCTP_ADDR_ANY,
		.bind_net = MCTP_NET_ANY, .bind_type = 1,
		.have_peer = true, .peer_addr = 20, .peer_net = MCTP_NET_ANY, },

	/* remote eid 20, net 1, local eid 11 */
	{ .name = "remote21local11", .bind_addr = 11,
		.bind_net = MCTP_NET_ANY, .bind_type = 1,
		.have_peer = true, .peer_addr = 21, .peer_net = MCTP_NET_ANY, },

	/* remote eid 21, specific net=3 for connect() */
	{ .name = "remote21net3", .bind_addr = MCTP_ADDR_ANY,
		.bind_net = MCTP_NET_ANY, .bind_type = 1,
		.have_peer = true, .peer_addr = 21, .peer_net = 3, },

	/* remote eid 21, net 4 for bind, specific net=4 for connect() */
	{ .name = "remote21net4", .bind_addr = MCTP_ADDR_ANY,
		.bind_net = 4, .bind_type = 1,
		.have_peer = true, .peer_addr = 21, .peer_net = 4, },

	/* remote eid 21, net 5 for bind, specific net=5 for connect() */
	{ .name = "remote21net5", .bind_addr = MCTP_ADDR_ANY,
		.bind_net = 5, .bind_type = 1,
		.have_peer = true, .peer_addr = 21, .peer_net = 5, },
};

static void mctp_bind_lookup_desc(const struct mctp_bind_lookup_test *t,
				  char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "{src %d dst %d ty %d net %d expect %s}",
		 t->hdr.src, t->hdr.dest, t->ty, t->net, t->expect);
}

KUNIT_ARRAY_PARAM(mctp_bind_lookup, mctp_bind_lookup_tests,
		  mctp_bind_lookup_desc);

static void mctp_test_bind_lookup(struct kunit *test)
{
	const struct mctp_bind_lookup_test *rx;
	struct socket *socks[ARRAY_SIZE(lookup_binds)];
	struct sk_buff *skb_pkt = NULL, *skb_sock = NULL;
	struct socket *sock_ty0, *sock_expect = NULL;
	struct mctp_test_pktqueue tpq;
	struct mctp_test_dev *dev;
	struct mctp_dst dst;
	int rc;

	rx = test->param_value;

	__mctp_route_test_init(test, &dev, &dst, &tpq, &sock_ty0, rx->net);
	/* Create all binds */
	for (size_t i = 0; i < ARRAY_SIZE(lookup_binds); i++) {
		mctp_test_bind_run(test, &lookup_binds[i],
				   &rc, &socks[i]);
		KUNIT_ASSERT_EQ(test, rc, 0);

		/* Record the expected receive socket */
		if (rx->expect &&
		    strcmp(rx->expect, lookup_binds[i].name) == 0) {
			KUNIT_ASSERT_NULL(test, sock_expect);
			sock_expect = socks[i];
		}
	}
	KUNIT_ASSERT_EQ(test, !!sock_expect, !!rx->expect);

	/* Create test message */
	skb_pkt = mctp_test_create_skb_data(&rx->hdr, &rx->ty);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb_pkt);
	mctp_test_skb_set_dev(skb_pkt, dev);
	mctp_test_pktqueue_init(&tpq);

	rc = mctp_dst_input(&dst, skb_pkt);
	if (rx->expect) {
		/* Test the message is received on the expected socket */
		KUNIT_EXPECT_EQ(test, rc, 0);
		skb_sock = skb_recv_datagram(sock_expect->sk,
					     MSG_DONTWAIT, &rc);
		if (!skb_sock) {
			/* Find which socket received it instead */
			for (size_t i = 0; i < ARRAY_SIZE(lookup_binds); i++) {
				skb_sock = skb_recv_datagram(socks[i]->sk,
							     MSG_DONTWAIT, &rc);
				if (skb_sock) {
					KUNIT_FAIL(test,
						   "received on incorrect socket '%s', expect '%s'",
						   lookup_binds[i].name,
						   rx->expect);
					goto cleanup;
				}
			}
			KUNIT_FAIL(test, "no message received");
		}
	} else {
		KUNIT_EXPECT_NE(test, rc, 0);
	}

cleanup:
	kfree_skb(skb_sock);

	/* Drop all binds */
	for (size_t i = 0; i < ARRAY_SIZE(lookup_binds); i++)
		sock_release(socks[i]);

	__mctp_route_test_fini(test, dev, &dst, &tpq, sock_ty0);
}

static struct kunit_case mctp_test_cases[] = {
	KUNIT_CASE_PARAM(mctp_test_fragment, mctp_frag_gen_params),
	KUNIT_CASE_PARAM(mctp_test_rx_input, mctp_rx_input_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk, mctp_route_input_sk_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk_reasm,
			 mctp_route_input_sk_reasm_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk_keys,
			 mctp_route_input_sk_keys_gen_params),
	KUNIT_CASE(mctp_test_route_input_sk_fail_single),
	KUNIT_CASE(mctp_test_route_input_sk_fail_frag),
	KUNIT_CASE(mctp_test_route_input_multiple_nets_bind),
	KUNIT_CASE(mctp_test_route_input_multiple_nets_key),
	KUNIT_CASE(mctp_test_packet_flow),
	KUNIT_CASE(mctp_test_fragment_flow),
	KUNIT_CASE(mctp_test_route_output_key_create),
	KUNIT_CASE(mctp_test_route_input_cloned_frag),
	KUNIT_CASE(mctp_test_route_extaddr_input),
	KUNIT_CASE(mctp_test_route_gw_lookup),
	KUNIT_CASE(mctp_test_route_gw_loop),
	KUNIT_CASE_PARAM(mctp_test_route_gw_mtu, mctp_route_gw_mtu_gen_params),
	KUNIT_CASE(mctp_test_route_gw_output),
	KUNIT_CASE_PARAM(mctp_test_bind_lookup, mctp_bind_lookup_gen_params),
	{}
};

static struct kunit_suite mctp_test_suite = {
	.name = "mctp-route",
	.test_cases = mctp_test_cases,
};

kunit_test_suite(mctp_test_suite);
