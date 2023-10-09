// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>
#include <linux/skbuff.h>

static const char hdr[] = "abcdefgh";
static const int gso_size = 1000;

static void __init_skb(struct sk_buff *skb)
{
	skb_reset_mac_header(skb);
	memcpy(skb_mac_header(skb), hdr, sizeof(hdr));

	/* skb_segment expects skb->data at start of payload */
	skb_pull(skb, sizeof(hdr));
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);

	/* proto is arbitrary, as long as not ETH_P_TEB or vlan */
	skb->protocol = htons(ETH_P_ATALK);
	skb_shinfo(skb)->gso_size = gso_size;
}

enum gso_test_nr {
	GSO_TEST_LINEAR,
	GSO_TEST_NO_GSO,
	GSO_TEST_FRAGS,
	GSO_TEST_FRAGS_PURE,
	GSO_TEST_GSO_PARTIAL,
};

struct gso_test_case {
	enum gso_test_nr id;
	const char *name;

	/* input */
	unsigned int linear_len;
	unsigned int nr_frags;
	const unsigned int *frags;

	/* output as expected */
	unsigned int nr_segs;
	const unsigned int *segs;
};

static struct gso_test_case cases[] = {
	{
		.id = GSO_TEST_NO_GSO,
		.name = "no_gso",
		.linear_len = gso_size,
		.nr_segs = 1,
		.segs = (const unsigned int[]) { gso_size },
	},
	{
		.id = GSO_TEST_LINEAR,
		.name = "linear",
		.linear_len = gso_size + gso_size + 1,
		.nr_segs = 3,
		.segs = (const unsigned int[]) { gso_size, gso_size, 1 },
	},
	{
		.id = GSO_TEST_FRAGS,
		.name = "frags",
		.linear_len = gso_size,
		.nr_frags = 2,
		.frags = (const unsigned int[]) { gso_size, 1 },
		.nr_segs = 3,
		.segs = (const unsigned int[]) { gso_size, gso_size, 1 },
	},
	{
		.id = GSO_TEST_FRAGS_PURE,
		.name = "frags_pure",
		.nr_frags = 3,
		.frags = (const unsigned int[]) { gso_size, gso_size, 2 },
		.nr_segs = 3,
		.segs = (const unsigned int[]) { gso_size, gso_size, 2 },
	},
	{
		.id = GSO_TEST_GSO_PARTIAL,
		.name = "gso_partial",
		.linear_len = gso_size,
		.nr_frags = 2,
		.frags = (const unsigned int[]) { gso_size, 3 },
		.nr_segs = 2,
		.segs = (const unsigned int[]) { 2 * gso_size, 3 },
	},
};

static void gso_test_case_to_desc(struct gso_test_case *t, char *desc)
{
	sprintf(desc, "%s", t->name);
}

KUNIT_ARRAY_PARAM(gso_test, cases, gso_test_case_to_desc);

static void gso_test_func(struct kunit *test)
{
	const int shinfo_size = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	const struct gso_test_case *tcase;
	struct sk_buff *skb, *segs, *cur;
	netdev_features_t features;
	struct page *page;
	int i;

	tcase = test->param_value;

	page = alloc_page(GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, page);
	skb = build_skb(page_address(page), sizeof(hdr) + tcase->linear_len + shinfo_size);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	__skb_put(skb, sizeof(hdr) + tcase->linear_len);

	__init_skb(skb);

	if (tcase->nr_frags) {
		unsigned int pg_off = 0;

		page = alloc_page(GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, page);
		page_ref_add(page, tcase->nr_frags - 1);

		for (i = 0; i < tcase->nr_frags; i++) {
			skb_fill_page_desc(skb, i, page, pg_off, tcase->frags[i]);
			pg_off += tcase->frags[i];
		}

		KUNIT_ASSERT_LE(test, pg_off, PAGE_SIZE);

		skb->data_len = pg_off;
		skb->len += skb->data_len;
		skb->truesize += skb->data_len;
	}

	features = NETIF_F_SG | NETIF_F_HW_CSUM;
	if (tcase->id == GSO_TEST_GSO_PARTIAL)
		features |= NETIF_F_GSO_PARTIAL;

	segs = skb_segment(skb, features);
	if (IS_ERR(segs)) {
		KUNIT_FAIL(test, "segs error %lld", PTR_ERR(segs));
		goto free_gso_skb;
	} else if (!segs) {
		KUNIT_FAIL(test, "no segments");
		goto free_gso_skb;
	}

	for (cur = segs, i = 0; cur; cur = cur->next, i++) {
		KUNIT_ASSERT_EQ(test, cur->len, sizeof(hdr) + tcase->segs[i]);

		/* segs have skb->data pointing to the mac header */
		KUNIT_ASSERT_PTR_EQ(test, skb_mac_header(cur), cur->data);
		KUNIT_ASSERT_PTR_EQ(test, skb_network_header(cur), cur->data + sizeof(hdr));

		/* header was copied to all segs */
		KUNIT_ASSERT_EQ(test, memcmp(skb_mac_header(cur), hdr, sizeof(hdr)), 0);

		/* last seg can be found through segs->prev pointer */
		if (!cur->next)
			KUNIT_ASSERT_PTR_EQ(test, cur, segs->prev);
	}

	KUNIT_ASSERT_EQ(test, i, tcase->nr_segs);

	consume_skb(segs);
free_gso_skb:
	consume_skb(skb);
}

static struct kunit_case gso_test_cases[] = {
	KUNIT_CASE_PARAM(gso_test_func, gso_test_gen_params),
	{}
};

static struct kunit_suite gso_test_suite = {
	.name = "net_core_gso",
	.test_cases = gso_test_cases,
};

kunit_test_suite(gso_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for segmentation offload");
