// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include "utils.h"

struct mctp_test_route {
	struct mctp_route	rt;
	struct sk_buff_head	pkts;
};

static int mctp_test_route_output(struct mctp_route *rt, struct sk_buff *skb)
{
	struct mctp_test_route *test_rt = container_of(rt, struct mctp_test_route, rt);

	skb_queue_tail(&test_rt->pkts, skb);

	return 0;
}

/* local version of mctp_route_alloc() */
static struct mctp_test_route *mctp_route_test_alloc(void)
{
	struct mctp_test_route *rt;

	rt = kzalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return NULL;

	INIT_LIST_HEAD(&rt->rt.list);
	refcount_set(&rt->rt.refs, 1);
	rt->rt.output = mctp_test_route_output;

	skb_queue_head_init(&rt->pkts);

	return rt;
}

static struct mctp_test_route *mctp_test_create_route(struct net *net,
						      struct mctp_dev *dev,
						      mctp_eid_t eid,
						      unsigned int mtu)
{
	struct mctp_test_route *rt;

	rt = mctp_route_test_alloc();
	if (!rt)
		return NULL;

	rt->rt.min = eid;
	rt->rt.max = eid;
	rt->rt.mtu = mtu;
	rt->rt.type = RTN_UNSPEC;
	if (dev)
		mctp_dev_hold(dev);
	rt->rt.dev = dev;

	list_add_rcu(&rt->rt.list, &net->mctp.routes);

	return rt;
}

static void mctp_test_route_destroy(struct kunit *test,
				    struct mctp_test_route *rt)
{
	unsigned int refs;

	rtnl_lock();
	list_del_rcu(&rt->rt.list);
	rtnl_unlock();

	skb_queue_purge(&rt->pkts);
	if (rt->rt.dev)
		mctp_dev_put(rt->rt.dev);

	refs = refcount_read(&rt->rt.refs);
	KUNIT_ASSERT_EQ_MSG(test, refs, 1, "route ref imbalance");

	kfree_rcu(&rt->rt, rcu);
}

static void mctp_test_skb_set_dev(struct sk_buff *skb,
				  struct mctp_test_dev *dev)
{
	struct mctp_skb_cb *cb;

	cb = mctp_cb(skb);
	cb->net = READ_ONCE(dev->mdev->net);
	skb->dev = dev->ndev;
}

static struct sk_buff *mctp_test_create_skb(const struct mctp_hdr *hdr,
					    unsigned int data_len)
{
	size_t hdr_len = sizeof(*hdr);
	struct sk_buff *skb;
	unsigned int i;
	u8 *buf;

	skb = alloc_skb(hdr_len + data_len, GFP_KERNEL);
	if (!skb)
		return NULL;

	__mctp_cb(skb);
	memcpy(skb_put(skb, hdr_len), hdr, hdr_len);

	buf = skb_put(skb, data_len);
	for (i = 0; i < data_len; i++)
		buf[i] = i & 0xff;

	return skb;
}

static struct sk_buff *__mctp_test_create_skb_data(const struct mctp_hdr *hdr,
						   const void *data,
						   size_t data_len)
{
	size_t hdr_len = sizeof(*hdr);
	struct sk_buff *skb;

	skb = alloc_skb(hdr_len + data_len, GFP_KERNEL);
	if (!skb)
		return NULL;

	__mctp_cb(skb);
	memcpy(skb_put(skb, hdr_len), hdr, hdr_len);
	memcpy(skb_put(skb, data_len), data, data_len);

	return skb;
}

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
	int rc, i, n, mtu, msgsize;
	struct mctp_test_route *rt;
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

	rt = mctp_test_create_route(&init_net, NULL, 10, mtu);
	KUNIT_ASSERT_TRUE(test, rt);

	rc = mctp_do_fragment_route(&rt->rt, skb, mtu, MCTP_TAG_OWNER);
	KUNIT_EXPECT_FALSE(test, rc);

	n = rt->pkts.qlen;

	KUNIT_EXPECT_EQ(test, n, params->n_frags);

	for (i = 0;; i++) {
		struct mctp_hdr *hdr2;
		struct sk_buff *skb2;
		u8 tag_mask, seq2;
		bool first, last;

		first = i == 0;
		last = i == (n - 1);

		skb2 = skb_dequeue(&rt->pkts);

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

	mctp_test_route_destroy(test, rt);
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
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct sk_buff *skb;

	params = test->param_value;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	rt = mctp_test_create_route(&init_net, dev->mdev, 8, 68);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

	skb = mctp_test_create_skb(&params->hdr, 1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	mctp_pkttype_receive(skb, dev->ndev, &mctp_packet_type, NULL);

	KUNIT_EXPECT_EQ(test, !!rt->pkts.qlen, params->input);

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
				   struct mctp_test_route **rtp,
				   struct socket **sockp,
				   unsigned int netid)
{
	struct sockaddr_mctp addr = {0};
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct socket *sock;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	if (netid != MCTP_NET_ANY)
		WRITE_ONCE(dev->mdev->net, netid);

	rt = mctp_test_create_route(&init_net, dev->mdev, 8, 68);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	addr.smctp_family = AF_MCTP;
	addr.smctp_network = netid;
	addr.smctp_addr.s_addr = 8;
	addr.smctp_type = 0;
	rc = kernel_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	KUNIT_ASSERT_EQ(test, rc, 0);

	*rtp = rt;
	*devp = dev;
	*sockp = sock;
}

static void __mctp_route_test_fini(struct kunit *test,
				   struct mctp_test_dev *dev,
				   struct mctp_test_route *rt,
				   struct socket *sock)
{
	sock_release(sock);
	mctp_test_route_destroy(test, rt);
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
	struct sk_buff *skb, *skb2;
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct socket *sock;
	int rc;

	params = test->param_value;

	__mctp_route_test_init(test, &dev, &rt, &sock, MCTP_NET_ANY);

	skb = mctp_test_create_skb_data(&params->hdr, &params->type);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	mctp_test_skb_set_dev(skb, dev);

	rc = mctp_route_input(&rt->rt, skb);

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

	__mctp_route_test_fini(test, dev, rt, sock);
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
	struct sk_buff *skb, *skb2;
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct socket *sock;
	int i, rc;
	u8 c;

	params = test->param_value;

	__mctp_route_test_init(test, &dev, &rt, &sock, MCTP_NET_ANY);

	for (i = 0; i < params->n_hdrs; i++) {
		c = i;
		skb = mctp_test_create_skb_data(&params->hdrs[i], &c);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

		mctp_test_skb_set_dev(skb, dev);

		rc = mctp_route_input(&rt->rt, skb);
	}

	skb2 = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);

	if (params->rx_len) {
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, skb2);
		KUNIT_EXPECT_EQ(test, skb2->len, params->rx_len);
		skb_free_datagram(sock->sk, skb2);

	} else {
		KUNIT_EXPECT_NULL(test, skb2);
	}

	__mctp_route_test_fini(test, dev, rt, sock);
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
	struct mctp_test_route *rt;
	struct sk_buff *skb, *skb2;
	struct mctp_test_dev *dev;
	struct mctp_sk_key *key;
	struct netns_mctp *mns;
	struct mctp_sock *msk;
	struct socket *sock;
	unsigned long flags;
	unsigned int net;
	int rc;
	u8 c;

	params = test->param_value;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	net = READ_ONCE(dev->mdev->net);

	rt = mctp_test_create_route(&init_net, dev->mdev, 8, 68);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

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

	rc = mctp_route_input(&rt->rt, skb);

	/* (potentially) receive message */
	skb2 = skb_recv_datagram(sock->sk, MSG_DONTWAIT, &rc);

	if (params->deliver)
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, skb2);
	else
		KUNIT_EXPECT_PTR_EQ(test, skb2, NULL);

	if (skb2)
		skb_free_datagram(sock->sk, skb2);

	mctp_key_unref(key);
	__mctp_route_test_fini(test, dev, rt, sock);
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
	struct mctp_test_route *rt;
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

	__mctp_route_test_init(test, &t->dev, &t->rt, &t->sock, t->netid);

	t->skb = mctp_test_create_skb_data(&hdr, &t->msg);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, t->skb);
	mctp_test_skb_set_dev(t->skb, t->dev);
}

static void
mctp_test_route_input_multiple_nets_bind_fini(struct kunit *test,
					      struct test_net *t)
{
	__mctp_route_test_fini(test, t->dev, t->rt, t->sock);
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

	rc = mctp_route_input(&t1.rt->rt, t1.skb);
	KUNIT_ASSERT_EQ(test, rc, 0);
	rc = mctp_route_input(&t2.rt->rt, t2.skb);
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

	__mctp_route_test_init(test, &t->dev, &t->rt, &t->sock, t->netid);

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
	__mctp_route_test_fini(test, t->dev, t->rt, t->sock);
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

	rc = mctp_route_input(&t1.rt->rt, t1.skb);
	KUNIT_ASSERT_EQ(test, rc, 0);
	rc = mctp_route_input(&t2.rt->rt, t2.skb);
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

#if IS_ENABLED(CONFIG_MCTP_FLOWS)

static void mctp_test_flow_init(struct kunit *test,
				struct mctp_test_dev **devp,
				struct mctp_test_route **rtp,
				struct socket **sock,
				struct sk_buff **skbp,
				unsigned int len)
{
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct sk_buff *skb;

	/* we have a slightly odd routing setup here; the test route
	 * is for EID 8, which is our local EID. We don't do a routing
	 * lookup, so that's fine - all we require is a path through
	 * mctp_local_output, which will call rt->output on whatever
	 * route we provide
	 */
	__mctp_route_test_init(test, &dev, &rt, sock, MCTP_NET_ANY);

	/* Assign a single EID. ->addrs is freed on mctp netdev release */
	dev->mdev->addrs = kmalloc(sizeof(u8), GFP_KERNEL);
	dev->mdev->num_addrs = 1;
	dev->mdev->addrs[0] = 8;

	skb = alloc_skb(len + sizeof(struct mctp_hdr) + 1, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, skb);
	__mctp_cb(skb);
	skb_reserve(skb, sizeof(struct mctp_hdr) + 1);
	memset(skb_put(skb, len), 0, len);

	/* take a ref for the route, we'll decrement in local output */
	refcount_inc(&rt->rt.refs);

	*devp = dev;
	*rtp = rt;
	*skbp = skb;
}

static void mctp_test_flow_fini(struct kunit *test,
				struct mctp_test_dev *dev,
				struct mctp_test_route *rt,
				struct socket *sock)
{
	__mctp_route_test_fini(test, dev, rt, sock);
}

/* test that an outgoing skb has the correct MCTP extension data set */
static void mctp_test_packet_flow(struct kunit *test)
{
	struct sk_buff *skb, *skb2;
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct mctp_flow *flow;
	struct socket *sock;
	u8 dst = 8;
	int n, rc;

	mctp_test_flow_init(test, &dev, &rt, &sock, &skb, 30);

	rc = mctp_local_output(sock->sk, &rt->rt, skb, dst, MCTP_TAG_OWNER);
	KUNIT_ASSERT_EQ(test, rc, 0);

	n = rt->pkts.qlen;
	KUNIT_ASSERT_EQ(test, n, 1);

	skb2 = skb_dequeue(&rt->pkts);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb2);

	flow = skb_ext_find(skb2, SKB_EXT_MCTP);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, flow);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, flow->key);
	KUNIT_ASSERT_PTR_EQ(test, flow->key->sk, sock->sk);

	kfree_skb(skb2);
	mctp_test_flow_fini(test, dev, rt, sock);
}

/* test that outgoing skbs, after fragmentation, all have the correct MCTP
 * extension data set.
 */
static void mctp_test_fragment_flow(struct kunit *test)
{
	struct mctp_flow *flows[2];
	struct sk_buff *tx_skbs[2];
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct sk_buff *skb;
	struct socket *sock;
	u8 dst = 8;
	int n, rc;

	mctp_test_flow_init(test, &dev, &rt, &sock, &skb, 100);

	rc = mctp_local_output(sock->sk, &rt->rt, skb, dst, MCTP_TAG_OWNER);
	KUNIT_ASSERT_EQ(test, rc, 0);

	n = rt->pkts.qlen;
	KUNIT_ASSERT_EQ(test, n, 2);

	/* both resulting packets should have the same flow data */
	tx_skbs[0] = skb_dequeue(&rt->pkts);
	tx_skbs[1] = skb_dequeue(&rt->pkts);

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
	mctp_test_flow_fini(test, dev, rt, sock);
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
	const unsigned int netid = 50;
	const u8 dst = 26, src = 15;
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct mctp_sk_key *key;
	struct netns_mctp *mns;
	unsigned long flags;
	struct socket *sock;
	struct sk_buff *skb;
	bool empty, single;
	const int len = 2;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	WRITE_ONCE(dev->mdev->net, netid);

	rt = mctp_test_create_route(&init_net, dev->mdev, dst, 68);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	dev->mdev->addrs = kmalloc(sizeof(u8), GFP_KERNEL);
	dev->mdev->num_addrs = 1;
	dev->mdev->addrs[0] = src;

	skb = alloc_skb(sizeof(struct mctp_hdr) + 1 + len, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, skb);
	__mctp_cb(skb);
	skb_reserve(skb, sizeof(struct mctp_hdr) + 1 + len);
	memset(skb_put(skb, len), 0, len);

	refcount_inc(&rt->rt.refs);

	mns = &sock_net(sock->sk)->mctp;

	/* We assume we're starting from an empty keys list, which requires
	 * preceding tests to clean up correctly!
	 */
	spin_lock_irqsave(&mns->keys_lock, flags);
	empty = hlist_empty(&mns->keys);
	spin_unlock_irqrestore(&mns->keys_lock, flags);
	KUNIT_ASSERT_TRUE(test, empty);

	rc = mctp_local_output(sock->sk, &rt->rt, skb, dst, MCTP_TAG_OWNER);
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
	KUNIT_EXPECT_EQ(test, key->local_addr, src);
	KUNIT_EXPECT_EQ(test, key->peer_addr, dst);
	/* key has incoming tag, so inverse of what we sent */
	KUNIT_EXPECT_FALSE(test, key->tag & MCTP_TAG_OWNER);

	sock_release(sock);
	mctp_test_route_destroy(test, rt);
	mctp_test_destroy_dev(dev);
}

static struct kunit_case mctp_test_cases[] = {
	KUNIT_CASE_PARAM(mctp_test_fragment, mctp_frag_gen_params),
	KUNIT_CASE_PARAM(mctp_test_rx_input, mctp_rx_input_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk, mctp_route_input_sk_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk_reasm,
			 mctp_route_input_sk_reasm_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk_keys,
			 mctp_route_input_sk_keys_gen_params),
	KUNIT_CASE(mctp_test_route_input_multiple_nets_bind),
	KUNIT_CASE(mctp_test_route_input_multiple_nets_key),
	KUNIT_CASE(mctp_test_packet_flow),
	KUNIT_CASE(mctp_test_fragment_flow),
	KUNIT_CASE(mctp_test_route_output_key_create),
	{}
};

static struct kunit_suite mctp_test_suite = {
	.name = "mctp",
	.test_cases = mctp_test_cases,
};

kunit_test_suite(mctp_test_suite);
