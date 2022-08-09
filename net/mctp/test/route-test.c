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

	/* The refcount would usually be incremented as part of a route lookup,
	 * but we're setting the route directly here.
	 */
	refcount_inc(&rt->rt.refs);

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

	__mctp_cb(skb);

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
				   struct socket **sockp)
{
	struct sockaddr_mctp addr;
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct socket *sock;
	int rc;

	dev = mctp_test_create_dev();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	rt = mctp_test_create_route(&init_net, dev->mdev, 8, 68);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	addr.smctp_family = AF_MCTP;
	addr.smctp_network = MCTP_NET_ANY;
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

	__mctp_route_test_init(test, &dev, &rt, &sock);

	skb = mctp_test_create_skb_data(&params->hdr, &params->type);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	skb->dev = dev->ndev;
	__mctp_cb(skb);

	rc = mctp_route_input(&rt->rt, skb);

	if (params->deliver) {
		KUNIT_EXPECT_EQ(test, rc, 0);

		skb2 = skb_recv_datagram(sock->sk, 0, 1, &rc);
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, skb2);
		KUNIT_EXPECT_EQ(test, skb->len, 1);

		skb_free_datagram(sock->sk, skb2);

	} else {
		KUNIT_EXPECT_NE(test, rc, 0);
		skb2 = skb_recv_datagram(sock->sk, 0, 1, &rc);
		KUNIT_EXPECT_PTR_EQ(test, skb2, NULL);
	}

	__mctp_route_test_fini(test, dev, rt, sock);
}

#define FL_S	(MCTP_HDR_FLAG_SOM)
#define FL_E	(MCTP_HDR_FLAG_EOM)
#define FL_T	(MCTP_HDR_FLAG_TO)

static const struct mctp_route_input_sk_test mctp_route_input_sk_tests[] = {
	{ .hdr = RX_HDR(1, 10, 8, FL_S | FL_E | FL_T), .type = 0, .deliver = true },
	{ .hdr = RX_HDR(1, 10, 8, FL_S | FL_E | FL_T), .type = 1, .deliver = false },
	{ .hdr = RX_HDR(1, 10, 8, FL_S | FL_E), .type = 0, .deliver = false },
	{ .hdr = RX_HDR(1, 10, 8, FL_E | FL_T), .type = 0, .deliver = false },
	{ .hdr = RX_HDR(1, 10, 8, FL_T), .type = 0, .deliver = false },
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

	__mctp_route_test_init(test, &dev, &rt, &sock);

	for (i = 0; i < params->n_hdrs; i++) {
		c = i;
		skb = mctp_test_create_skb_data(&params->hdrs[i], &c);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

		skb->dev = dev->ndev;
		__mctp_cb(skb);

		rc = mctp_route_input(&rt->rt, skb);
	}

	skb2 = skb_recv_datagram(sock->sk, 0, 1, &rc);

	if (params->rx_len) {
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, skb2);
		KUNIT_EXPECT_EQ(test, skb2->len, params->rx_len);
		skb_free_datagram(sock->sk, skb2);

	} else {
		KUNIT_EXPECT_PTR_EQ(test, skb2, NULL);
	}

	__mctp_route_test_fini(test, dev, rt, sock);
}

#define RX_FRAG(f, s) RX_HDR(1, 10, 8, FL_T | (f) | ((s) << MCTP_HDR_SEQ_SHIFT))

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

static struct kunit_case mctp_test_cases[] = {
	KUNIT_CASE_PARAM(mctp_test_fragment, mctp_frag_gen_params),
	KUNIT_CASE_PARAM(mctp_test_rx_input, mctp_rx_input_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk, mctp_route_input_sk_gen_params),
	KUNIT_CASE_PARAM(mctp_test_route_input_sk_reasm,
			 mctp_route_input_sk_reasm_gen_params),
	{}
};

static struct kunit_suite mctp_test_suite = {
	.name = "mctp",
	.test_cases = mctp_test_cases,
};

kunit_test_suite(mctp_test_suite);
