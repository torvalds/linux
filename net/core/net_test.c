// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>

/* GSO */

#include <linux/skbuff.h>
#include <net/gso.h>

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
	GSO_TEST_FRAG_LIST_HEAD_FRAG,
	GSO_TEST_FRAG_LIST_NO_HEAD_FRAG,
	GSO_TEST_GSO_BY_FRAGS,
	GSO_TEST_RESEGMENT,
	GSO_TEST_RESEGMENT_MULTI,
	GSO_TEST_RESEGMENT_TAIL,
	GSO_TEST_RESEGMENT_ONE_MSS,
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
	unsigned int max_segs;
	const unsigned int *frag_skb_frags;
	unsigned int gso_type;
	/* skbs from alloc_skb() instead of build_skb(), head_frag clear */
	bool no_head_frag;

	/* output as expected */
	unsigned int nr_segs;
	const unsigned int *segs;
	const unsigned int *gso_segs;
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
		/* commit 13acc94eff12: frag_list members with linear data in a
		 * page fragment, shared as a page frag of the segment
		 */
		.id = GSO_TEST_FRAG_LIST_HEAD_FRAG,
		.name = "frag_list_head_frag",
		.linear_len = 8,
		.nr_frags = 1,
		.frags = (const unsigned int[]) { 64 },
		.nr_frag_skbs = 1,
		.frag_skbs = (const unsigned int[]) { 8 },
		.frag_skb_frags = (const unsigned int[]) { 64 },
		.gso_type = SKB_GSO_DODGY,
		.nr_segs = 1,
		.segs = (const unsigned int[]) { 144 },
	},
	{
		/* commit 3dcbdb134f32: gso_size smaller than the linear data,
		 * as after bpf_skb_change_proto() grew the header, with
		 * frag_list members whose linear data is not in a page fragment
		 */
		.id = GSO_TEST_FRAG_LIST_NO_HEAD_FRAG,
		.name = "frag_list_no_head_frag",
		.linear_len = GSO_TEST_SIZE + 20,
		.nr_frag_skbs = 1,
		.frag_skbs = (const unsigned int[]) { GSO_TEST_SIZE + 20 },
		.gso_type = SKB_GSO_DODGY,
		.no_head_frag = true,
		.nr_segs = 3,
		.segs = (const unsigned int[]) { GSO_TEST_SIZE, GSO_TEST_SIZE, 40 },
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
	{
		.id = GSO_TEST_RESEGMENT,
		.name = "resegment",
		.linear_len = GSO_TEST_SIZE,
		.nr_frags = 3,
		.frags = (const unsigned int[]) {
			GSO_TEST_SIZE, GSO_TEST_SIZE, 3,
		},
		.max_segs = 2,
		.nr_segs = 2,
		.segs = (const unsigned int[]) {
			2 * GSO_TEST_SIZE, GSO_TEST_SIZE + 3,
		},
		.gso_segs = (const unsigned int[]) { 2, 2 },
	},
	{
		.id = GSO_TEST_RESEGMENT_MULTI,
		.name = "resegment_multi",
		.linear_len = 2 * GSO_TEST_SIZE,
		.nr_frags = 4,
		.frags = (const unsigned int[]) {
			GSO_TEST_SIZE, GSO_TEST_SIZE, GSO_TEST_SIZE, 3,
		},
		.max_segs = 2,
		.nr_segs = 3,
		.segs = (const unsigned int[]) {
			2 * GSO_TEST_SIZE, 2 * GSO_TEST_SIZE, GSO_TEST_SIZE + 3,
		},
		.gso_segs = (const unsigned int[]) { 2, 2, 2 },
	},
	{
		/* The last output is not a GSO skb. */
		.id = GSO_TEST_RESEGMENT_TAIL,
		.name = "resegment_tail",
		.linear_len = GSO_TEST_SIZE,
		.nr_frags = 2,
		.frags = (const unsigned int[]) {
			GSO_TEST_SIZE, 400,
		},
		.max_segs = 2,
		.nr_segs = 2,
		.segs = (const unsigned int[]) {
			2 * GSO_TEST_SIZE, 400,
		},
		.gso_segs = (const unsigned int[]) { 2, 0 },
	},
	{
		/* max_segs of 1 is the same as no limit. */
		.id = GSO_TEST_RESEGMENT_ONE_MSS,
		.name = "resegment_one_mss",
		.linear_len = GSO_TEST_SIZE,
		.nr_frags = 3,
		.frags = (const unsigned int[]) {
			GSO_TEST_SIZE, GSO_TEST_SIZE, 3,
		},
		.max_segs = 1,
		.nr_segs = 4,
		.segs = (const unsigned int[]) {
			GSO_TEST_SIZE, GSO_TEST_SIZE, GSO_TEST_SIZE, 3,
		},
		.gso_segs = (const unsigned int[]) { 0, 0, 0, 0 },
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

	if (tcase->no_head_frag) {
		skb = alloc_skb(sizeof(hdr) + tcase->linear_len, GFP_KERNEL);
	} else {
		page = alloc_page(GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, page);
		skb = build_skb(page_address(page), sizeof(hdr) + tcase->linear_len + shinfo_size);
	}
	KUNIT_ASSERT_NOT_NULL(test, skb);
	__skb_put(skb, sizeof(hdr) + tcase->linear_len);

	__init_skb(skb);
	skb_shinfo(skb)->gso_type = tcase->gso_type;

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
			unsigned int frag_size, frag_len;

			frag_size = tcase->frag_skbs[i];
			if (tcase->no_head_frag) {
				frag_skb = alloc_skb(frag_size, GFP_KERNEL);
			} else {
				page = alloc_page(GFP_KERNEL);
				KUNIT_ASSERT_NOT_NULL(test, page);
				frag_skb = build_skb(page_address(page),
						     frag_size + shinfo_size);
			}
			KUNIT_ASSERT_NOT_NULL(test, frag_skb);
			__skb_put(frag_skb, frag_size);

			frag_len = tcase->frag_skb_frags ? tcase->frag_skb_frags[i] : 0;
			if (frag_len) {
				page = alloc_page(GFP_KERNEL);
				KUNIT_ASSERT_NOT_NULL(test, page);
				skb_fill_page_desc(frag_skb, 0, page, 0, frag_len);

				frag_skb->data_len = frag_len;
				frag_skb->len += frag_len;
				frag_skb->truesize += frag_len;
			}

			if (prev)
				prev->next = frag_skb;
			else
				skb_shinfo(skb)->frag_list = frag_skb;
			prev = frag_skb;

			total_size += frag_skb->len;
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

	SKB_GSO_CB(skb)->max_segs = tcase->max_segs;
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

		if (i >= tcase->nr_segs)
			goto consume;

		KUNIT_ASSERT_EQ(test, cur->len, sizeof(hdr) + tcase->segs[i]);

		/* segs have skb->data pointing to the mac header */
		KUNIT_ASSERT_PTR_EQ(test, skb_mac_header(cur), cur->data);
		KUNIT_ASSERT_PTR_EQ(test, skb_network_header(cur), cur->data + sizeof(hdr));

		/* header was copied to all segs */
		KUNIT_ASSERT_EQ(test, memcmp(skb_mac_header(cur), hdr, sizeof(hdr)), 0);
		if (tcase->gso_segs) {
			unsigned int gso_segs = tcase->gso_segs[i];

			KUNIT_EXPECT_EQ(test, skb_shinfo(cur)->gso_segs,
					gso_segs);
			if (!gso_segs) {
				KUNIT_EXPECT_FALSE(test, skb_is_gso(cur));
			} else {
				KUNIT_EXPECT_TRUE(test, skb_is_gso(cur));
				KUNIT_EXPECT_EQ(test, skb_shinfo(cur)->gso_size,
						GSO_TEST_SIZE);
			}
		}

		/* last seg can be found through segs->prev pointer */
		if (!next)
			KUNIT_ASSERT_PTR_EQ(test, cur, last);

consume:
		consume_skb(cur);
	}

	KUNIT_ASSERT_EQ(test, i, tcase->nr_segs);

free_gso_skb:
	consume_skb(skb);
}

/* IP tunnel flags */

#include <net/ip_tunnels.h>

struct ip_tunnel_flags_test {
	const char	*name;

	const u16	*src_bits;
	const u16	*exp_bits;
	u8		src_num;
	u8		exp_num;

	__be16		exp_val;
	bool		exp_comp;
};

#define IP_TUNNEL_FLAGS_TEST(n, src, comp, eval, exp) {	\
	.name		= (n),				\
	.src_bits	= (src),			\
	.src_num	= ARRAY_SIZE(src),		\
	.exp_comp	= (comp),			\
	.exp_val	= (eval),			\
	.exp_bits	= (exp),			\
	.exp_num	= ARRAY_SIZE(exp),		\
}

/* These are __be16-compatible and can be compared as is */
static const u16 ip_tunnel_flags_1[] = {
	IP_TUNNEL_KEY_BIT,
	IP_TUNNEL_STRICT_BIT,
	IP_TUNNEL_ERSPAN_OPT_BIT,
};

/* Due to the previous flags design limitation, setting either
 * ``IP_TUNNEL_CSUM_BIT`` (on Big Endian) or ``IP_TUNNEL_DONT_FRAGMENT_BIT``
 * (on Little) also sets VTI/ISATAP bit. In the bitmap implementation, they
 * correspond to ``BIT(16)``, which is bigger than ``U16_MAX``, but still is
 * backward-compatible.
 */
#ifdef __LITTLE_ENDIAN
#define IP_TUNNEL_CONFLICT_BIT	IP_TUNNEL_DONT_FRAGMENT_BIT
#else
#define IP_TUNNEL_CONFLICT_BIT	IP_TUNNEL_CSUM_BIT
#endif

static const u16 ip_tunnel_flags_2_src[] = {
	IP_TUNNEL_CONFLICT_BIT,
};

static const u16 ip_tunnel_flags_2_exp[] = {
	IP_TUNNEL_CONFLICT_BIT,
	IP_TUNNEL_SIT_ISATAP_BIT,
};

/* Bits 17 and higher are not compatible with __be16 flags */
static const u16 ip_tunnel_flags_3_src[] = {
	IP_TUNNEL_VXLAN_OPT_BIT,
	17,
	18,
	20,
};

static const u16 ip_tunnel_flags_3_exp[] = {
	IP_TUNNEL_VXLAN_OPT_BIT,
};

static const struct ip_tunnel_flags_test ip_tunnel_flags_test[] = {
	IP_TUNNEL_FLAGS_TEST("compat", ip_tunnel_flags_1, true,
			     cpu_to_be16(BIT(IP_TUNNEL_KEY_BIT) |
					 BIT(IP_TUNNEL_STRICT_BIT) |
					 BIT(IP_TUNNEL_ERSPAN_OPT_BIT)),
			     ip_tunnel_flags_1),
	IP_TUNNEL_FLAGS_TEST("conflict", ip_tunnel_flags_2_src, true,
			     VTI_ISVTI, ip_tunnel_flags_2_exp),
	IP_TUNNEL_FLAGS_TEST("new", ip_tunnel_flags_3_src, false,
			     cpu_to_be16(BIT(IP_TUNNEL_VXLAN_OPT_BIT)),
			     ip_tunnel_flags_3_exp),
};

static void
ip_tunnel_flags_test_case_to_desc(const struct ip_tunnel_flags_test *t,
				  char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}
KUNIT_ARRAY_PARAM(ip_tunnel_flags_test, ip_tunnel_flags_test,
		  ip_tunnel_flags_test_case_to_desc);

static void ip_tunnel_flags_test_run(struct kunit *test)
{
	const struct ip_tunnel_flags_test *t = test->param_value;
	IP_TUNNEL_DECLARE_FLAGS(src) = { };
	IP_TUNNEL_DECLARE_FLAGS(exp) = { };
	IP_TUNNEL_DECLARE_FLAGS(out);

	for (u32 j = 0; j < t->src_num; j++)
		__set_bit(t->src_bits[j], src);
	for (u32 j = 0; j < t->exp_num; j++)
		__set_bit(t->exp_bits[j], exp);

	KUNIT_ASSERT_EQ(test, t->exp_comp,
			ip_tunnel_flags_is_be16_compat(src));
	KUNIT_ASSERT_EQ(test, (__force u16)t->exp_val,
			(__force u16)ip_tunnel_flags_to_be16(src));

	ip_tunnel_flags_from_be16(out, t->exp_val);
	KUNIT_ASSERT_TRUE(test, __ipt_flag_op(bitmap_equal, exp, out));
}

/* Netmem & SKB fragment refcounting */

#include <linux/skbuff_ref.h>
#include <net/netmem.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/helpers.h>
#include <net/page_pool/memory_provider.h>
#include <net/tcp.h>
#include "devmem.h"
#include "netmem_priv.h"

static void netmem_ref_test_page(struct kunit *test)
{
	struct sk_buff *skb;
	netmem_ref netmem;
	struct page *page;
	int base_ref;

	page = alloc_page(GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, page);
	netmem = page_to_netmem(page);
	base_ref = page_ref_count(page);

	/* Layer 1: get_netmem / put_netmem use page non-pp refcount */
	get_netmem(netmem);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref + 1);
	put_netmem(netmem);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

#ifdef CONFIG_PAGE_POOL
	/* Layer 2: napi_pp_get/put_page return false on non-PP page */
	KUNIT_EXPECT_FALSE(test, napi_pp_get_page(netmem));
	KUNIT_EXPECT_FALSE(test, napi_pp_put_page(netmem));
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
#endif

	/* Layer 3: skb_netmem_ref / unref fall back to non-pp on non-PP page */
	skb_netmem_ref(netmem, false);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref + 1);
	skb_netmem_unref(netmem, false);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

	skb_netmem_ref(netmem, true);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref + 1);
	skb_netmem_unref(netmem, true);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

	/* Layer 4: skb_frag_ref / skb_frag_unref on non-PP page */
	skb = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);

	skb->pp_recycle = 1;
	skb_frag_ref(skb, 0);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref + 1);
	skb_frag_unref(skb, 0);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

	skb_shinfo(skb)->nr_frags = 0;
	consume_skb(skb);
	__free_page(page);
}

#ifdef CONFIG_PAGE_POOL
static void netmem_ref_test_pp_page(struct kunit *test)
{
	struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 4,
		.nid = NUMA_NO_NODE,
	};
	struct page_pool *pool;
	struct sk_buff *skb, *copy, *clone, *split;
	netmem_ref netmem;
	struct page *page;
	long base_pp_ref;
	int base_ref;

	pool = page_pool_create(&pp_params);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pool);

	page = page_pool_dev_alloc_pages(pool);
	KUNIT_ASSERT_NOT_NULL(test, page);
	netmem = page_to_netmem(page);
	base_ref = page_ref_count(page);
	base_pp_ref = atomic_long_read(&page->pp_ref_count);

	/* Layer 1: get_netmem / put_netmem always use non-pp refcount */
	get_netmem(netmem);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref + 1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);
	put_netmem(netmem);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

	/* Layer 2: napi_pp_get/put_page use pp_ref_count */
	KUNIT_EXPECT_TRUE(test, napi_pp_get_page(netmem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_TRUE(test, napi_pp_put_page(netmem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* Layer 3: skb_netmem_ref / unref obey recycle flag */
	skb_netmem_ref(netmem, false);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref + 1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);
	skb_netmem_unref(netmem, false);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

	skb_netmem_ref(netmem, true);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	skb_netmem_unref(netmem, true);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* Layer 4: skb_frag_ref / skb_frag_unref match on pp_recycle=1 & 0 */
	skb = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len = 64;
	skb->data_len = 64;

	skb->pp_recycle = 1;
	skb_frag_ref(skb, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	skb_frag_unref(skb, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* pskb_copy on pp_recycle=1 skb propagates pp_recycle & pp_ref_count */
	copy = pskb_copy(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, copy);
	KUNIT_EXPECT_TRUE(test, copy->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(copy);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* skb_zerocopy on pp_recycle=1 skb propagates pp_ref_count */
	copy = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, copy);
	skb_put(copy, skb_tailroom(copy));
	skb->head_frag = 1;
	KUNIT_ASSERT_EQ(test, skb_zerocopy(copy, skb, skb->len, 0), 0);
	skb->head_frag = 0;
	KUNIT_EXPECT_TRUE(test, copy->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(copy);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* skb_clone + pskb_expand_head preserves pp_recycle and pp_ref_count */
	clone = skb_clone(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone);
	KUNIT_ASSERT_EQ(test, pskb_expand_head(clone, 32, 0, GFP_KERNEL), 0);
	KUNIT_EXPECT_TRUE(test, clone->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(clone);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* skb_split inside linear header & inside nonlinear page_pool frags */
	skb_shinfo(skb)->nr_frags = 0;
	skb->len = 0;
	skb->data_len = 0;
	skb_reset_tail_pointer(skb);
	skb_put(skb, 32);
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len += 64;
	skb->data_len = 64;
	split = alloc_skb(32, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, split);
	skb_split(skb, split, 16);
	KUNIT_EXPECT_TRUE(test, split->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	skb_shinfo(split)->nr_frags = 0;
	consume_skb(split);
	skb->len = 0;
	skb->data_len = 0;
	skb_reset_tail_pointer(skb);
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len = 64;
	skb->data_len = 64;

	split = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, split);
	skb_split(skb, split, 32);
	KUNIT_EXPECT_TRUE(test, split->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(split);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* skb_segment on pp_recycle=1 skb propagates pp_recycle to segments */
	skb->protocol = htons(ETH_P_IP);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_shinfo(skb)->gso_size = 16;
	split = skb_segment(skb, NETIF_F_SG | NETIF_F_HW_CSUM);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(split));
	KUNIT_EXPECT_TRUE(test, split->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	kfree_skb_list(split);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* skb_shift balances pp_ref_count across split, partial coalesce,
	 * multi-frag coalesce + partial next-frag shift, and full merge.
	 */
	skb_fill_netmem_desc(skb, 0, netmem, 0, 32);
	skb_fill_netmem_desc(skb, 1, netmem, 64, 32);
	page_pool_ref_page(page);
	skb->len = 64;
	skb->data_len = 64;
	split = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, split);
	split->pp_recycle = 1;
	KUNIT_EXPECT_EQ(test, skb_shift(split, skb, 16), 16);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, skb_shift(split, skb, 8), 8);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, skb_shift(split, skb, 24), 24);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_EQ(test, skb_shift(split, skb, 16), 16);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	skb_shinfo(split)->nr_frags = 0;
	consume_skb(split);
	page_pool_put_full_page(pool, page, false);
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len = 64;
	skb->data_len = 64;

	skb->pp_recycle = 0;
	skb_frag_ref(skb, 0);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref + 1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);
	skb_frag_unref(skb, 0);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

	skb_shinfo(skb)->nr_frags = 0;
	consume_skb(skb);
	page_pool_put_full_page(pool, page, false);
	page_pool_destroy(pool);

	/* Order-1 compound page_pool page: tail netmem resolves to head &
	 * build_skb(head_frag=1, pp_recycle=1) skb_zerocopy uses pp_ref_count.
	 */
	pp_params.order = 1;
	pool = page_pool_create(&pp_params);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pool);
	page = page_pool_dev_alloc_pages(pool);
	KUNIT_ASSERT_NOT_NULL(test, page);
	base_ref = page_ref_count(page);
	base_pp_ref = atomic_long_read(&page->pp_ref_count);

	netmem = page_to_netmem(page + 1);
	KUNIT_EXPECT_TRUE(test, napi_pp_get_page(netmem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	KUNIT_EXPECT_TRUE(test, napi_pp_put_page(netmem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	skb = build_skb(page_address(page), PAGE_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb->pp_recycle = 1;
	skb_put(skb, 64);
	page_pool_ref_page(page);

	copy = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, copy);
	skb_put(copy, skb_tailroom(copy));
	KUNIT_ASSERT_EQ(test, skb_zerocopy(copy, skb, skb->len, 0), 0);
	KUNIT_EXPECT_TRUE(test, copy->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(copy);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	consume_skb(skb);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	page_pool_put_full_page(pool, page, false);
	page_pool_destroy(pool);
}
#endif

#if defined(CONFIG_PAGE_POOL) && defined(CONFIG_NET_DEVMEM)
static void dummy_percpu_ref_release(struct percpu_ref *ref)
{
}

static void netmem_ref_test_net_iov(struct kunit *test)
{
	struct net_devmem_dmabuf_binding binding = {};
	struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 4,
		.nid = NUMA_NO_NODE,
	};
	struct sk_buff *skb, *clone, *copy;
	struct page_pool *pool;
	struct net_iov niov = {};
	netmem_ref netmem;
	long base_ref;
	int err;

	err = percpu_ref_init(&binding.ref, dummy_percpu_ref_release,
			      PERCPU_REF_INIT_ATOMIC, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	net_iov_init(&niov, &binding.area, NET_IOV_DMABUF);
	netmem = net_iov_to_netmem(&niov);
	base_ref = atomic_long_read(&binding.ref.data->count);

	/* Non-PP net_iov: net_iov_inc/dec_ref & get/put_netmem */
	net_iov_inc_ref(&niov);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref + 1);
	net_iov_dec_ref(&niov);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);

	KUNIT_EXPECT_FALSE(test, napi_pp_get_page(netmem));
	KUNIT_EXPECT_FALSE(test, napi_pp_put_page(netmem));

	skb_netmem_ref(netmem, true);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref + 1);
	skb_netmem_unref(netmem, true);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);

	/* Attach net_iov to a page_pool to test PP net_iov behavior */
	pool = page_pool_create(&pp_params);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pool);
	netmem_set_pp(netmem, pool);
	netmem_or_pp_magic(netmem, PP_SIGNATURE);
	atomic_long_set(&niov.desc.pp_ref_count, 2);

	/* Layer 0 & 1: net_iov_inc_ref / get_netmem grab non-pp ref on PP */
	net_iov_inc_ref(&niov);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref + 1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);
	net_iov_dec_ref(&niov);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);

	/* Layer 2: napi_pp_get/put_page use pp_ref_count */
	KUNIT_EXPECT_TRUE(test, napi_pp_get_page(netmem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);
	KUNIT_EXPECT_TRUE(test, napi_pp_put_page(netmem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	/* Layer 4: skb_frag_ref / skb_frag_unref on PP net_iov */
	skb = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len = 64;
	skb->data_len = 64;

	skb->pp_recycle = 1;
	skb_frag_ref(skb, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);
	skb_frag_unref(skb, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	/* pskb_copy on pp_recycle=1 net_iov skb keeps pp_ref_count */
	copy = pskb_copy(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, copy);
	KUNIT_EXPECT_TRUE(test, copy->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);
	consume_skb(copy);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	/* skb_zerocopy on pp_recycle=1 net_iov skb keeps pp_ref_count */
	copy = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, copy);
	skb_put(copy, skb_tailroom(copy));
	skb->head_frag = 1;
	skb->unreadable = 1;
	KUNIT_ASSERT_EQ(test, skb_zerocopy(copy, skb, skb->len, 0), 0);
	skb->head_frag = 0;
	KUNIT_EXPECT_TRUE(test, copy->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);
	consume_skb(copy);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	/* skb_shift balances pp_ref_count on split and merge */
	copy = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, copy);
	copy->pp_recycle = 1;
	KUNIT_EXPECT_EQ(test, skb_shift(copy, skb, 16), 16);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);
	KUNIT_EXPECT_EQ(test, skb_shift(copy, skb, 48), 48);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);
	skb_shinfo(copy)->nr_frags = 0;
	consume_skb(copy);
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len = 64;
	skb->data_len = 64;

	/* Uncloning a cloned pp_recycle=1 net_iov skb keeps pp_ref_count */
	clone = skb_clone(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone);
	KUNIT_ASSERT_EQ(test, pskb_expand_head(skb, 32, 0, GFP_KERNEL), 0);
	KUNIT_EXPECT_TRUE(test, skb->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);
	consume_skb(clone);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	skb->pp_recycle = 0;
	skb_frag_ref(skb, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref + 1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);
	skb_frag_unref(skb, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_ref);

	skb_shinfo(skb)->nr_frags = 0;
	consume_skb(skb);
	netmem_clear_pp_magic(netmem);
	netmem_set_pp(netmem, NULL);
	page_pool_destroy(pool);
	percpu_ref_exit(&binding.ref);
}

static void netmem_ref_test_release_data(struct kunit *test)
{
	struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 4,
		.nid = NUMA_NO_NODE,
	};
	struct sk_buff *skb, *clone1, *clone2, *ext;
	struct net_devmem_dmabuf_binding binding = {};
	struct net_iov niov = {};
	struct page_pool *pool;
	struct page *page;
	netmem_ref netmem;
	long base_pp_ref;
	int base_ref;
	int i;

	pool = page_pool_create(&pp_params);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pool));
	page = page_pool_dev_alloc_pages(pool);
	KUNIT_ASSERT_NOT_NULL(test, page);
	netmem = page_to_netmem(page);
	page_pool_ref_netmem(netmem);

	/* 3-way clone chain: only last clone drops pp_ref_count */
	skb = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_put(skb, 32);
	skb->pp_recycle = 1;
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len += 64;
	skb->data_len = 64;
	base_ref = page_ref_count(page);
	base_pp_ref = atomic_long_read(&page->pp_ref_count);

	page_pool_ref_netmem(netmem);
	clone1 = skb_clone(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone1);
	clone2 = skb_clone(clone1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone2);

	/* Freeing head skb first leaves clone1/clone2 pp_recycle=1 intact */
	consume_skb(skb);
	KUNIT_EXPECT_TRUE(test, clone1->pp_recycle);
	KUNIT_EXPECT_TRUE(test, clone2->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	consume_skb(clone2);
	KUNIT_EXPECT_TRUE(test, clone1->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	consume_skb(clone1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref);

	/* All 4 combinations of (unclone skb vs clone) x (free order) */
	for (i = 0; i < 4; i++) {
		skb = alloc_skb(64, GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, skb);
		skb_put(skb, 32);
		skb->pp_recycle = 1;
		skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
		skb->len += 64;
		skb->data_len = 64;
		page_pool_ref_netmem(netmem);

		clone1 = skb_clone(skb, GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, clone1);
		KUNIT_ASSERT_EQ(test,
				pskb_expand_head((i & 1) ? clone1 : skb,
						 32, 0, GFP_KERNEL), 0);
		KUNIT_EXPECT_TRUE(test, skb->pp_recycle);
		KUNIT_EXPECT_TRUE(test, clone1->pp_recycle);
		KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
				base_pp_ref + 2);
		KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);

		if (i & 2) {
			consume_skb(clone1);
			KUNIT_EXPECT_EQ(test,
					atomic_long_read(&page->pp_ref_count),
					base_pp_ref + 1);
			consume_skb(skb);
		} else {
			consume_skb(skb);
			KUNIT_EXPECT_EQ(test,
					atomic_long_read(&page->pp_ref_count),
					base_pp_ref + 1);
			consume_skb(clone1);
		}
		KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
				base_pp_ref);
		KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	}

	/* pskb_extract carving inside header & inside nonlinear frags */
	skb = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_put(skb, 32);
	skb->pp_recycle = 1;
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len += 64;
	skb->data_len = 64;
	page_pool_ref_netmem(netmem);

	/* Carve inside header keeping frag -> pp_ref_count +1 until free */
	ext = pskb_extract(skb, 16, skb->len - 16, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ext);
	KUNIT_EXPECT_TRUE(test, ext->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(ext);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);

	/* Carve + trim + skb_condense ref/unrefs pp_ref_count cleanly */
	ext = pskb_extract(skb, 16, 48, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ext);
	KUNIT_EXPECT_TRUE(test, ext->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(ext);

	/* Carve inside nonlinear keeping frag -> pp_ref_count +1 until free */
	ext = pskb_extract(skb, 48, skb->len - 48, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ext);
	KUNIT_EXPECT_TRUE(test, ext->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(page), base_ref);
	consume_skb(ext);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);

	/* skb_morph drops old skb data via skb_release_data */
	page_pool_ref_netmem(netmem);
	clone1 = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone1);
	clone1->pp_recycle = 1;
	skb_fill_netmem_desc(clone1, 0, netmem, 0, 64);
	clone1->len = 64;
	clone1->data_len = 64;
	skb_morph(clone1, skb);
	KUNIT_EXPECT_TRUE(test, clone1->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&page->pp_ref_count),
			base_pp_ref + 1);
	consume_skb(clone1);
	kfree_skb(skb);
	page_pool_put_full_page(pool, page, false);
	page_pool_put_full_page(pool, page, false);

	/* Also verify pskb_extract on page_pool net_iov */
	KUNIT_ASSERT_EQ(test,
			percpu_ref_init(&binding.ref, dummy_percpu_ref_release,
					PERCPU_REF_INIT_ATOMIC, GFP_KERNEL),
			0);
	net_iov_init(&niov, &binding.area, NET_IOV_DMABUF);
	netmem = net_iov_to_netmem(&niov);
	netmem_set_pp(netmem, pool);
	netmem_or_pp_magic(netmem, PP_SIGNATURE);
	atomic_long_set(&niov.desc.pp_ref_count, 2);

	skb = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_put(skb, 32);
	skb->pp_recycle = 1;
	skb_fill_netmem_desc(skb, 0, netmem, 0, 64);
	skb->len += 64;
	skb->data_len = 64;

	ext = pskb_extract(skb, 48, skb->len - 48, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ext);
	KUNIT_EXPECT_TRUE(test, ext->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	consume_skb(ext);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	skb_shinfo(skb)->nr_frags = 0;
	consume_skb(skb);
	netmem_clear_pp_magic(netmem);
	netmem_set_pp(netmem, NULL);
	page_pool_destroy(pool);
	percpu_ref_exit(&binding.ref);
}

static void netmem_ref_test_skb_segment(struct kunit *test)
{
	struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 4,
		.nid = NUMA_NO_NODE,
	};
	struct ubuf_info uarg = {
		.flags = SKBFL_ZEROCOPY_ENABLE,
	};
	struct sk_buff *head, *list, *segs, *seg;
	struct net_devmem_dmabuf_binding binding = {};
	struct net_iov niov = {};
	struct page *pp_page, *reg_page;
	netmem_ref pp_nm, niov_nm;
	struct page_pool *pool;
	long base_pp_ref, base_iov_ref;
	int base_pp_page_ref, base_reg_ref;

	pool = page_pool_create(&pp_params);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pool));
	pp_page = page_pool_dev_alloc_pages(pool);
	KUNIT_ASSERT_NOT_NULL(test, pp_page);
	pp_nm = page_to_netmem(pp_page);
	page_pool_ref_netmem(pp_nm);
	reg_page = alloc_page(GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, reg_page);

	KUNIT_ASSERT_EQ(test,
			percpu_ref_init(&binding.ref, dummy_percpu_ref_release,
					PERCPU_REF_INIT_ATOMIC, GFP_KERNEL),
			0);
	net_iov_init(&niov, &binding.area, NET_IOV_DMABUF);
	niov_nm = net_iov_to_netmem(&niov);
	netmem_set_pp(niov_nm, pool);
	netmem_or_pp_magic(niov_nm, PP_SIGNATURE);
	atomic_long_set(&niov.desc.pp_ref_count, 2);

	base_pp_ref = atomic_long_read(&pp_page->pp_ref_count);
	base_pp_page_ref = page_ref_count(pp_page);
	base_reg_ref = page_ref_count(reg_page);
	base_iov_ref = atomic_long_read(&binding.ref.data->count);

	/* 1. Single GSO skb with page_pool net_iov frags */
	head = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, head);
	head->pp_recycle = 1;
	head->protocol = htons(ETH_P_IP);
	skb_reset_mac_header(head);
	skb_reset_network_header(head);
	skb_fill_netmem_desc(head, 0, niov_nm, 0, 64);
	head->len = 64;
	head->data_len = 64;
	skb_shinfo(head)->gso_size = 16;

	segs = skb_segment(head, NETIF_F_SG | NETIF_F_HW_CSUM);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(segs));
	for (seg = segs; seg; seg = seg->next)
		KUNIT_EXPECT_TRUE(test, seg->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 6L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_iov_ref);
	kfree_skb_list(segs);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);
	skb_shinfo(head)->nr_frags = 0;
	consume_skb(head);

	/* 2. GRO frag_list fast path (skb_clone of pp_recycle=1 list_skb) */
	head = alloc_skb(64, GFP_KERNEL);
	list = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, head);
	KUNIT_ASSERT_NOT_NULL(test, list);
	head->pp_recycle = 1;
	list->pp_recycle = 1;
	head->protocol = htons(ETH_P_IP);
	skb_reset_mac_header(head);
	skb_reset_network_header(head);
	skb_put(head, 16);
	skb_fill_netmem_desc(head, 0, pp_nm, 0, 16);
	head->len += 16;
	head->data_len = 16;
	page_pool_ref_netmem(pp_nm);

	skb_put(list, 16);
	skb_fill_netmem_desc(list, 0, pp_nm, 16, 16);
	list->len += 16;
	list->data_len = 16;
	page_pool_ref_netmem(pp_nm);

	skb_shinfo(head)->frag_list = list;
	head->len += list->len;
	head->data_len += list->len;
	skb_shinfo(head)->gso_size = 32;

	segs = skb_segment(head, NETIF_F_SG | NETIF_F_HW_CSUM);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(segs));
	for (seg = segs; seg; seg = seg->next)
		KUNIT_EXPECT_TRUE(test, seg->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 4);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	kfree_skb_list(segs);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 2);
	consume_skb(head);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);

	/* 3. Mixed GRO frag_list (head pp_recycle=1 + list pp_recycle=0):
	 * straddling segment linearizes its pp_recycle=1 prefix before
	 * attaching the pp_recycle=0 frag so no nskb mixes incompatible
	 * fragment refcount types; non-straddling segments stay zero-copy SG.
	 */
	head = alloc_skb(64, GFP_KERNEL);
	list = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, head);
	KUNIT_ASSERT_NOT_NULL(test, list);
	head->pp_recycle = 1;
	list->pp_recycle = 0;
	head->protocol = htons(ETH_P_IP);
	skb_reset_mac_header(head);
	skb_reset_network_header(head);
	skb_fill_netmem_desc(head, 0, pp_nm, 0, 24);
	head->len = 24;
	head->data_len = 24;
	page_pool_ref_netmem(pp_nm);

	skb_fill_page_desc(list, 0, reg_page, 0, 24);
	get_page(reg_page);
	list->len = 24;
	list->data_len = 24;

	skb_shinfo(head)->frag_list = list;
	head->len += list->len;
	head->data_len += list->len;
	skb_shinfo(head)->gso_size = 16;

	segs = skb_segment(head, NETIF_F_SG | NETIF_F_HW_CSUM);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(segs));
	KUNIT_EXPECT_TRUE(test, segs->pp_recycle);
	KUNIT_EXPECT_FALSE(test, segs->next->pp_recycle);
	KUNIT_EXPECT_FALSE(test, segs->next->next->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(reg_page), base_reg_ref + 3);
	kfree_skb_list(segs);
	consume_skb(head);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(reg_page), base_reg_ref);

	/* 4. Reverse mixed frag_list (head pp_recycle=0 + list pp_recycle=1):
	 * Crossing from pp_recycle=0 head into pp_recycle=1 list_skb must not
	 * elevate page_ref_count(pp_page) instead of pp_ref_count.
	 */
	head = alloc_skb(64, GFP_KERNEL);
	list = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, head);
	KUNIT_ASSERT_NOT_NULL(test, list);
	head->pp_recycle = 0;
	list->pp_recycle = 1;
	head->protocol = htons(ETH_P_IP);
	skb_reset_mac_header(head);
	skb_reset_network_header(head);
	skb_fill_page_desc(head, 0, reg_page, 0, 24);
	get_page(reg_page);
	head->len = 24;
	head->data_len = 24;

	skb_fill_netmem_desc(list, 0, pp_nm, 0, 24);
	page_pool_ref_netmem(pp_nm);
	list->len = 24;
	list->data_len = 24;

	skb_shinfo(head)->frag_list = list;
	head->len += list->len;
	head->data_len += list->len;
	skb_shinfo(head)->gso_size = 16;

	segs = skb_segment(head, NETIF_F_SG | NETIF_F_HW_CSUM);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(segs));
	KUNIT_EXPECT_FALSE(test, segs->pp_recycle);
	KUNIT_EXPECT_TRUE(test, segs->next->pp_recycle);
	KUNIT_EXPECT_TRUE(test, segs->next->next->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 3);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(reg_page), base_reg_ref + 2);
	kfree_skb_list(segs);
	consume_skb(head);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(reg_page), base_reg_ref);

	/* 5. Linear head_skb (nr_frags=0) + mixed pp_recycle frag_list where
	 * member A has linear + frag (pp_recycle=1) and tail member B has
	 * linear len == skb_headlen(A) (pp_recycle=0): keeps SG clone path
	 * synchronized without hitting BUG_ON(i >= nfrags).
	 */
	head = alloc_skb(64, GFP_KERNEL);
	list = alloc_skb(64, GFP_KERNEL);
	seg = alloc_skb(64, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, head);
	KUNIT_ASSERT_NOT_NULL(test, list);
	KUNIT_ASSERT_NOT_NULL(test, seg);
	head->pp_recycle = 0;
	list->pp_recycle = 1;
	seg->pp_recycle = 0;
	head->protocol = htons(ETH_P_IP);
	skb_reset_mac_header(head);
	skb_reset_network_header(head);
	skb_put(head, 32);
	skb_put(list, 8);
	skb_fill_netmem_desc(list, 0, pp_nm, 0, 24);
	page_pool_ref_netmem(pp_nm);
	list->len += 24;
	list->data_len = 24;
	skb_put(seg, 8);
	list->next = seg;
	skb_shinfo(head)->frag_list = list;
	head->len += list->len + seg->len;
	head->data_len = list->len + seg->len;
	skb_shinfo(head)->gso_size = 32;

	segs = skb_segment(head, NETIF_F_SG | NETIF_F_HW_CSUM);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(segs));
	KUNIT_EXPECT_FALSE(test, segs->pp_recycle);
	KUNIT_EXPECT_TRUE(test, segs->next->pp_recycle);
	KUNIT_EXPECT_FALSE(test, segs->next->next->pp_recycle);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	kfree_skb_list(segs);
	consume_skb(head);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);

	/* 6. skb_zerocopy -EFAULT rollback when from->head_frag (pp_page) is
	 * followed by unreadable net_iov frag drops pp_ref_count via
	 * skb_frag_unref(to, 0).
	 */
	head = build_skb(page_address(pp_page), PAGE_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, head);
	head->pp_recycle = 1;
	skb_put(head, 32);
	page_pool_ref_netmem(pp_nm);
	skb_fill_netmem_desc(head, 0, niov_nm, 0, 32);
	napi_pp_get_page(niov_nm);
	head->len += 32;
	head->data_len += 32;

	seg = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, seg);
	skb_put(seg, skb_tailroom(seg));
	KUNIT_EXPECT_EQ(test, skb_zerocopy(seg, head, head->len, 0), -EFAULT);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 1);
	skb_shinfo(seg)->nr_frags = 0;
	consume_skb(seg);
	consume_skb(head);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	/* 7. skb_zerocopy -ENOMEM rollback when skb_orphan_frags() fails on
	 * shared zerocopy skb drops pp_ref_count via skb_frag_unref(to, 0).
	 */
	head = build_skb(page_address(pp_page), PAGE_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, head);
	head->pp_recycle = 1;
	skb_put(head, 32);
	page_pool_ref_netmem(pp_nm);
	skb_zcopy_init(head, &uarg);
	skb_get(head);

	seg = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, seg);
	skb_put(seg, skb_tailroom(seg));
	KUNIT_EXPECT_EQ(test, skb_zerocopy(seg, head, head->len, 0), -ENOMEM);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 1);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	skb_shinfo(seg)->nr_frags = 0;
	consume_skb(seg);
	consume_skb(head);
	skb_shinfo(head)->flags &= ~SKBFL_ZEROCOPY_ENABLE;
	consume_skb(head);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);

	page_pool_put_full_page(pool, pp_page, false);
	page_pool_put_full_page(pool, pp_page, false);
	__free_page(reg_page);
	netmem_clear_pp_magic(niov_nm);
	netmem_set_pp(niov_nm, NULL);
	page_pool_destroy(pool);
	percpu_ref_exit(&binding.ref);
}

static void netmem_ref_test_try_coalesce(struct kunit *test)
{
	struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 4,
		.nid = NUMA_NO_NODE,
	};
	struct sk_buff *to, *from, *clone;
	struct net_devmem_dmabuf_binding binding = {};
	struct net_iov niov = {};
	struct page *pp_page, *reg_page;
	netmem_ref pp_nm, niov_nm;
	struct page_pool *pool;
	long base_pp_ref, base_iov_ref;
	int base_pp_page_ref, base_reg_ref;
	bool fragstolen;
	int delta;

	pool = page_pool_create(&pp_params);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pool));
	pp_page = page_pool_dev_alloc_pages(pool);
	KUNIT_ASSERT_NOT_NULL(test, pp_page);
	pp_nm = page_to_netmem(pp_page);
	page_pool_ref_netmem(pp_nm);

	reg_page = alloc_page(GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, reg_page);

	KUNIT_ASSERT_EQ(test,
			percpu_ref_init(&binding.ref, dummy_percpu_ref_release,
					PERCPU_REF_INIT_ATOMIC, GFP_KERNEL),
			0);
	net_iov_init(&niov, &binding.area, NET_IOV_DMABUF);
	niov_nm = net_iov_to_netmem(&niov);
	netmem_set_pp(niov_nm, pool);
	netmem_or_pp_magic(niov_nm, PP_SIGNATURE);
	atomic_long_set(&niov.desc.pp_ref_count, 2);

	base_pp_ref = atomic_long_read(&pp_page->pp_ref_count);
	base_pp_page_ref = page_ref_count(pp_page);
	base_reg_ref = page_ref_count(reg_page);
	base_iov_ref = atomic_long_read(&binding.ref.data->count);

	/* 1. Uncloned pp_recycle=1 from: frags stolen without extra ref */
	to = alloc_skb(0, GFP_KERNEL);
	from = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, to);
	KUNIT_ASSERT_NOT_NULL(test, from);
	skb_put(to, skb_tailroom(to));
	to->pp_recycle = 1;
	from->pp_recycle = 1;
	skb_add_rx_frag(from, 0, pp_page, 0, 64, PAGE_SIZE);
	page_pool_ref_netmem(pp_nm);
	KUNIT_EXPECT_TRUE(test, skb_try_coalesce(to, from, &fragstolen,
						 &delta));
	KUNIT_EXPECT_EQ(test, skb_shinfo(from)->nr_frags, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 1);
	consume_skb(from);
	consume_skb(to);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);

	/* 2. Cloned pp_recycle=1 from (pp_page + non-PP reg_page fallback):
	 * skb_frag_ref(from, i) increments pp_ref_count for pp_page and
	 * page_ref_count for reg_page.
	 */
	to = alloc_skb(0, GFP_KERNEL);
	from = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, to);
	KUNIT_ASSERT_NOT_NULL(test, from);
	skb_put(to, skb_tailroom(to));
	to->pp_recycle = 1;
	from->pp_recycle = 1;
	skb_add_rx_frag(from, 0, pp_page, 0, 32, PAGE_SIZE);
	page_pool_ref_netmem(pp_nm);
	skb_add_rx_frag(from, 1, reg_page, 0, 32, PAGE_SIZE);
	get_page(reg_page);

	clone = skb_clone(from, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone);
	KUNIT_EXPECT_TRUE(test, skb_try_coalesce(to, from, &fragstolen,
						 &delta));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref + 2);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(reg_page), base_reg_ref + 2);
	consume_skb(from);
	consume_skb(clone);
	consume_skb(to);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&pp_page->pp_ref_count),
			base_pp_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(pp_page), base_pp_page_ref);
	KUNIT_EXPECT_EQ(test, page_ref_count(reg_page), base_reg_ref);

	/* 3. Cloned unreadable pp_recycle=1 net_iov skb: skb_try_coalesce
	 * increments niov.desc.pp_ref_count via skb_frag_ref(from, i).
	 */
	to = alloc_skb(0, GFP_KERNEL);
	from = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, to);
	KUNIT_ASSERT_NOT_NULL(test, from);
	skb_put(to, skb_tailroom(to));
	to->pp_recycle = 1;
	to->unreadable = 1;
	from->pp_recycle = 1;
	skb_add_rx_frag_netmem(from, 0, niov_nm, 0, 64, PAGE_SIZE);
	napi_pp_get_page(niov_nm);

	clone = skb_clone(from, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone);
	KUNIT_EXPECT_TRUE(test, skb_try_coalesce(to, from, &fragstolen,
						 &delta));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 4L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_iov_ref);
	consume_skb(from);
	consume_skb(clone);
	consume_skb(to);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	page_pool_put_full_page(pool, pp_page, false);
	page_pool_put_full_page(pool, pp_page, false);
	__free_page(reg_page);
	netmem_clear_pp_magic(niov_nm);
	netmem_set_pp(niov_nm, NULL);
	page_pool_destroy(pool);
	percpu_ref_exit(&binding.ref);
}

#ifdef CONFIG_INET
static void netmem_ref_test_tcp_devmem_recv(struct kunit *test)
{
	char cmsg_buf[CMSG_SPACE(sizeof(struct dmabuf_cmsg))]
		__aligned(__alignof__(struct cmsghdr)) = {};
	struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 4,
		.nid = NUMA_NO_NODE,
	};
	struct net_devmem_dmabuf_binding binding = {};
	struct msghdr msg = {};
	struct dmabuf_token token;
	struct dmabuf_cmsg *cmsg;
	struct net_iov niov = {};
	struct page_pool *pool;
	struct cmsghdr *cm;
	struct socket *sock;
	struct sk_buff *skb;
	struct kvec iov = {};
	netmem_ref niov_nm;
	long base_iov_ref;
	int ret;

	KUNIT_ASSERT_EQ(test,
			sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
					 IPPROTO_TCP, &sock),
			0);
	sock->sk->sk_state = TCP_ESTABLISHED;
	inet_csk(sock->sk)->icsk_ack.rcv_mss = 64;
	tcp_sk(sock->sk)->copied_seq = 0;

	pool = page_pool_create(&pp_params);
	if (IS_ERR(pool)) {
		sock->sk->sk_state = TCP_CLOSE;
		sock_release(sock);
		KUNIT_ASSERT_FALSE(test, IS_ERR(pool));
	}

	ret = percpu_ref_init(&binding.ref, dummy_percpu_ref_release,
			      PERCPU_REF_INIT_ATOMIC, GFP_KERNEL);
	if (ret) {
		page_pool_destroy(pool);
		sock->sk->sk_state = TCP_CLOSE;
		sock_release(sock);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}
	binding.area.niovs = &niov;
	net_iov_init(&niov, &binding.area, NET_IOV_DMABUF);
	niov_nm = net_iov_to_netmem(&niov);
	netmem_set_pp(niov_nm, pool);
	netmem_or_pp_magic(niov_nm, PP_SIGNATURE);
	atomic_long_set(&niov.desc.pp_ref_count, 2);
	base_iov_ref = atomic_long_read(&binding.ref.data->count);

	/* 1. Receive via tcp_recvmsg_dmabuf -> SO_DEVMEM_DONTNEED release */
	skb = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb->pp_recycle = 1;
	skb_add_rx_frag_netmem(skb, 0, niov_nm, 0, 64, PAGE_SIZE);
	napi_pp_get_page(niov_nm);
	TCP_SKB_CB(skb)->seq = 0;
	TCP_SKB_CB(skb)->end_seq = 64;
	skb_queue_tail(&sock->sk->sk_receive_queue, skb);

	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);
	ret = kernel_recvmsg(sock, &msg, &iov, 0, 64,
			     MSG_SOCK_DEVMEM | MSG_DONTWAIT);
	KUNIT_ASSERT_EQ(test, ret, 64);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&binding.ref.data->count),
			base_iov_ref);

	cm = (struct cmsghdr *)cmsg_buf;
	cmsg = CMSG_DATA(cm);
	token.token_start = cmsg->frag_token;
	token.token_count = 1;
	KUNIT_EXPECT_EQ(test,
			sock_setsockopt(sock, SOL_SOCKET, SO_DEVMEM_DONTNEED,
					KERNEL_SOCKPTR(&token), sizeof(token)),
			1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	/* 2. Receive second packet & destroy socket to test
	 * tcp_release_user_frags() -> napi_pp_put_page().
	 */
	skb = alloc_skb(0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb->pp_recycle = 1;
	skb_add_rx_frag_netmem(skb, 0, niov_nm, 0, 64, PAGE_SIZE);
	napi_pp_get_page(niov_nm);
	TCP_SKB_CB(skb)->seq = 64;
	TCP_SKB_CB(skb)->end_seq = 128;
	skb_queue_tail(&sock->sk->sk_receive_queue, skb);

	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);
	ret = kernel_recvmsg(sock, &msg, &iov, 0, 64,
			     MSG_SOCK_DEVMEM | MSG_DONTWAIT);
	KUNIT_ASSERT_EQ(test, ret, 64);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 3L);

	sock->sk->sk_state = TCP_CLOSE;
	sock_release(sock);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&niov.desc.pp_ref_count), 2L);

	netmem_clear_pp_magic(niov_nm);
	netmem_set_pp(niov_nm, NULL);
	page_pool_destroy(pool);
	percpu_ref_exit(&binding.ref);
}
#endif
#endif

static struct kunit_case net_test_cases[] = {
	KUNIT_CASE_PARAM(gso_test_func, gso_test_gen_params),
	KUNIT_CASE_PARAM(ip_tunnel_flags_test_run,
			 ip_tunnel_flags_test_gen_params),
	KUNIT_CASE(netmem_ref_test_page),
#ifdef CONFIG_PAGE_POOL
	KUNIT_CASE(netmem_ref_test_pp_page),
#endif
#if defined(CONFIG_PAGE_POOL) && defined(CONFIG_NET_DEVMEM)
	KUNIT_CASE(netmem_ref_test_net_iov),
	KUNIT_CASE(netmem_ref_test_release_data),
	KUNIT_CASE(netmem_ref_test_skb_segment),
	KUNIT_CASE(netmem_ref_test_try_coalesce),
#ifdef CONFIG_INET
	KUNIT_CASE(netmem_ref_test_tcp_devmem_recv),
#endif
#endif
	{ },
};

static struct kunit_suite net_test_suite = {
	.name		= "net_core",
	.test_cases	= net_test_cases,
};
kunit_test_suite(net_test_suite);

MODULE_DESCRIPTION("KUnit tests for networking core");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_LICENSE("GPL");
