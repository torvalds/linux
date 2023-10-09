// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>
#include <linux/skbuff.h>

static const char hdr[] = "abcdefgh";
static const int gso_size = 1000, last_seg_size = 1;

/* default: create 3 segment gso packet */
static int payload_len = (2 * gso_size) + last_seg_size;

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

static void gso_test_func(struct kunit *test)
{
	const int shinfo_size = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	struct sk_buff *skb, *segs, *cur;
	struct page *page;

	page = alloc_page(GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, page);
	skb = build_skb(page_address(page), sizeof(hdr) + payload_len + shinfo_size);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	__skb_put(skb, sizeof(hdr) + payload_len);

	__init_skb(skb);

	segs = skb_segment(skb, NETIF_F_SG | NETIF_F_HW_CSUM);
	if (IS_ERR(segs)) {
		KUNIT_FAIL(test, "segs error %lld", PTR_ERR(segs));
		goto free_gso_skb;
	} else if (!segs) {
		KUNIT_FAIL(test, "no segments");
		goto free_gso_skb;
	}

	for (cur = segs; cur; cur = cur->next) {
		/* segs have skb->data pointing to the mac header */
		KUNIT_ASSERT_PTR_EQ(test, skb_mac_header(cur), cur->data);
		KUNIT_ASSERT_PTR_EQ(test, skb_network_header(cur), cur->data + sizeof(hdr));

		/* header was copied to all segs */
		KUNIT_ASSERT_EQ(test, memcmp(skb_mac_header(cur), hdr, sizeof(hdr)), 0);

		/* all segs are gso_size, except for last */
		if (cur->next) {
			KUNIT_ASSERT_EQ(test, cur->len, sizeof(hdr) + gso_size);
		} else {
			KUNIT_ASSERT_EQ(test, cur->len, sizeof(hdr) + last_seg_size);

			/* last seg can be found through segs->prev pointer */
			KUNIT_ASSERT_PTR_EQ(test, cur, segs->prev);
		}
	}

	consume_skb(segs);
free_gso_skb:
	consume_skb(skb);
}

static struct kunit_case gso_test_cases[] = {
	KUNIT_CASE(gso_test_func),
	{}
};

static struct kunit_suite gso_test_suite = {
	.name = "net_core_gso",
	.test_cases = gso_test_cases,
};

kunit_test_suite(gso_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for segmentation offload");
