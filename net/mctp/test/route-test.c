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

static void mctp_test_route_destroy(struct mctp_test_route *rt)
{
	rtnl_lock();
	list_del_rcu(&rt->rt.list);
	rtnl_unlock();

	skb_queue_purge(&rt->pkts);
	if (rt->rt.dev)
		mctp_dev_put(rt->rt.dev);

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

	mctp_test_route_destroy(rt);
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

	mctp_test_route_destroy(rt);
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

static struct kunit_case mctp_test_cases[] = {
	KUNIT_CASE_PARAM(mctp_test_fragment, mctp_frag_gen_params),
	KUNIT_CASE_PARAM(mctp_test_rx_input, mctp_rx_input_gen_params),
	{}
};

static struct kunit_suite mctp_test_suite = {
	.name = "mctp",
	.test_cases = mctp_test_cases,
};

kunit_test_suite(mctp_test_suite);
