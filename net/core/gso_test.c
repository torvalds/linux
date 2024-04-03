// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>
#include <linux/skbuff.h>

static const char hdr[] = "abcdefgh";
#define GSO_TEST_SIZE 1000

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
	skb_shinfo(skb)->gso_size = GSO_TEST_SIZE;
}

enum gso_test_nr {
	GSO_TEST_LINEAR,
	GSO_TEST_NO_GSO,
	GSO_TEST_FRAGS,
	GSO_TEST_FRAGS_PURE,
	GSO_TEST_GSO_PARTIAL,
	GSO_TEST_FRAG_LIST,
	GSO_TEST_FRAG_LIST_PURE,
	GSO_TEST_FRAG_LIST_NON_UNIFORM,
	GSO_TEST_GSO_BY_FRAGS,
};

struct gso_test_case {
	enum gso_test_nr id;
	const char *name;

	/* input */
	unsigned int linear_len;
	unsigned int nr_frags;
	const unsigned int *frags;
	unsigned int nr_frag_skbs;
	const unsigned int *frag_skbs;

	/* output as expected */
	unsigned int nr_segs;
	const unsigned int *segs;
};

static struct gso_test_case cases[] = {
	{
		.id = GSO_TEST_NO_GSO,
		.name = "no_gso",
		.linear_len = GSO_TEST_SIZE,
		.nr_segs = 1,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE },
	},
	{
		.id = GSO_TEST_LINEAR,
		.name = "linear",
		.linear_len = GSO_TEST_SIZE + GSO_TEST_SIZE + 1,
		.nr_segs = 3,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE, 1 },
	},
	{
		.id = GSO_TEST_FRAGS,
		.name = "frags",
		.linear_len = GSO_TEST_SIZE,
		.nr_frags = 2,
		.frags = (const unsigned int[]) { GSO_TEST_SIZE, 1 },
		.nr_segs = 3,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE, 1 },
	},
	{
		.id = GSO_TEST_FRAGS_PURE,
		.name = "frags_pure",
		.nr_frags = 3,
		.frags = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE, 2 },
		.nr_segs = 3,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE, 2 },
	},
	{
		.id = GSO_TEST_GSO_PARTIAL,
		.name = "gso_partial",
		.linear_len = GSO_TEST_SIZE,
		.nr_frags = 2,
		.frags = (const unsigned int[]) { GSO_TEST_SIZE, 3 },
		.nr_segs = 2,
		.segs = (const unsigned int[]) { 2 * GSO_TEST_SIZE, 3 },
	},
	{
		/* commit 89319d3801d1: frag_list on mss boundaries */
		.id = GSO_TEST_FRAG_LIST,
		.name = "frag_list",
		.linear_len = GSO_TEST_SIZE,
		.nr_frag_skbs = 2,
		.frag_skbs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE },
		.nr_segs = 3,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE, GSO_TEST_SIZE },
	},
	{
		.id = GSO_TEST_FRAG_LIST_PURE,
		.name = "frag_list_pure",
		.nr_frag_skbs = 2,
		.frag_skbs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE },
		.nr_segs = 2,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE },
	},
	{
		/* commit 43170c4e0ba7: GRO of frag_list trains */
		.id = GSO_TEST_FRAG_LIST_NON_UNIFORM,
		.name = "frag_list_non_uniform",
		.linear_len = GSO_TEST_SIZE,
		.nr_frag_skbs = 4,
		.frag_skbs = (const unsigned int[]) { GSO_TEST_SIZE, 1, GSO_TEST_SIZE, 2 },
		.nr_segs = 4,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE, GSO_TEST_SIZE, 3 },
	},
	{
		/* commit 3953c46c3ac7 ("sk_buff: allow segmenting based on frag sizes") and
		 * commit 90017accff61 ("sctp: Add GSO support")
		 *
		 * "there will be a cover skb with protocol headers and
		 *  children ones containing the actual segments"
		 */
		.id = GSO_TEST_GSO_BY_FRAGS,
		.name = "gso_by_frags",
		.nr_frag_skbs = 4,
		.frag_skbs = (const unsigned int[]) { 100, 200, 300, 400 },
		.nr_segs = 4,
		.segs = (const unsigned int[]) { 100, 200, 300, 400 },
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
	struct sk_buff *skb, *segs, *cur, *next, *last;
	const struct gso_test_case *tcase;
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

	if (tcase->frag_skbs) {
		unsigned int total_size = 0, total_true_size = 0;
		struct sk_buff *frag_skb, *prev = NULL;

		for (i = 0; i < tcase->nr_frag_skbs; i++) {
			unsigned int frag_size;

			page = alloc_page(GFP_KERNEL);
			KUNIT_ASSERT_NOT_NULL(test, page);

			frag_size = tcase->frag_skbs[i];
			frag_skb = build_skb(page_address(page),
					     frag_size + shinfo_size);
			KUNIT_ASSERT_NOT_NULL(test, frag_skb);
			__skb_put(frag_skb, frag_size);

			if (prev)
				prev->next = frag_skb;
			else
				skb_shinfo(skb)->frag_list = frag_skb;
			prev = frag_skb;

			total_size += frag_size;
			total_true_size += frag_skb->truesize;
		}

		skb->len += total_size;
		skb->data_len += total_size;
		skb->truesize += total_true_size;

		if (tcase->id == GSO_TEST_GSO_BY_FRAGS)
			skb_shinfo(skb)->gso_size = GSO_BY_FRAGS;
	}

	features = NETIF_F_SG | NETIF_F_HW_CSUM;
	if (tcase->id == GSO_TEST_GSO_PARTIAL)
		features |= NETIF_F_GSO_PARTIAL;

	/* TODO: this should also work with SG,
	 * rather than hit BUG_ON(i >= nfrags)
	 */
	if (tcase->id == GSO_TEST_FRAG_LIST_NON_UNIFORM)
		features &= ~NETIF_F_SG;

	segs = skb_segment(skb, features);
	if (IS_ERR(segs)) {
		KUNIT_FAIL(test, "segs error %pe", segs);
		goto free_gso_skb;
	} else if (!segs) {
		KUNIT_FAIL(test, "no segments");
		goto free_gso_skb;
	}

	last = segs->prev;
	for (cur = segs, i = 0; cur; cur = next, i++) {
		next = cur->next;

		KUNIT_ASSERT_EQ(test, cur->len, sizeof(hdr) + tcase->segs[i]);

		/* segs have skb->data pointing to the mac header */
		KUNIT_ASSERT_PTR_EQ(test, skb_mac_header(cur), cur->data);
		KUNIT_ASSERT_PTR_EQ(test, skb_network_header(cur), cur->data + sizeof(hdr));

		/* header was copied to all segs */
		KUNIT_ASSERT_EQ(test, memcmp(skb_mac_header(cur), hdr, sizeof(hdr)), 0);

		/* last seg can be found through segs->prev pointer */
		if (!next)
			KUNIT_ASSERT_PTR_EQ(test, cur, last);

		consume_skb(cur);
	}

	KUNIT_ASSERT_EQ(test, i, tcase->nr_segs);

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
