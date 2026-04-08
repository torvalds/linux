// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026, Oracle and/or its affiliates. */
#include <test_progs.h>
#include <linux/btf.h>
#include "bpf/libbpf_internal.h"
#include "../test_btf.h"
#include "kfree_skb.skel.h"

#define TYPE_LEN	(sizeof(struct btf_type) + sizeof(__u32))
#define MAX_NR_LAYOUT	2
#define LAYOUT_LEN	(sizeof(struct btf_layout) * MAX_NR_LAYOUT)
#define STR_LEN		sizeof("\0int")

struct layout_btf {
	struct btf_header hdr;
	__u32 types[TYPE_LEN/sizeof(__u32)];
	struct btf_layout layout[MAX_NR_LAYOUT];
	char strs[STR_LEN];
};

static const struct layout_btf layout_btf = {
	.hdr = {
		.magic		= BTF_MAGIC,
		.version	= BTF_VERSION,
		.hdr_len	= sizeof(struct btf_header),
		.type_off	= 0,
		.type_len	= TYPE_LEN,
		.str_off	= TYPE_LEN + LAYOUT_LEN,
		.str_len	= STR_LEN,
		.layout_off	= TYPE_LEN,
		.layout_len	= LAYOUT_LEN,
	},
	.types = {
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),
	},
	.layout = {
		{ .info_sz = 0,          .elem_sz = 0, .flags = 0 },
		{ .info_sz = sizeof(__u32), .elem_sz = 0, .flags = 0 },
	},
	.strs = "\0int",
};

void test_btf_sanitize_layout(void)
{
	struct btf *orig = NULL, *sanitized = NULL;
	struct kern_feature_cache *cache = NULL;
	struct kfree_skb *skel = NULL;
	const struct btf_header *hdr;
	const void *raw;
	__u32 raw_sz;

	skel = kfree_skb__open();
	if (!ASSERT_OK_PTR(skel, "kfree_skb_skel"))
		return;
	orig = btf__new(&layout_btf, sizeof(layout_btf));
	if (!ASSERT_OK_PTR(orig, "btf_new_layout"))
		goto out;
	raw = btf__raw_data(orig, &raw_sz);
	if (!ASSERT_OK_PTR(raw, "btf__raw_data_orig"))
		goto out;
	hdr = (struct btf_header *)raw;
	ASSERT_EQ(hdr->layout_off, TYPE_LEN, "layout_off_nonzero");
	ASSERT_EQ(hdr->layout_len, LAYOUT_LEN, "layout_len_nonzero");

	cache = calloc(1, sizeof(*cache));
	if (!ASSERT_OK_PTR(cache, "alloc_feat_cache"))
		goto out;
	for (int i = 0; i < __FEAT_CNT; i++)
		cache->res[i] = FEAT_SUPPORTED;
	cache->res[FEAT_BTF_LAYOUT] = FEAT_MISSING;

	bpf_object_set_feat_cache(skel->obj, cache);

	if (!ASSERT_FALSE(kernel_supports(skel->obj, FEAT_BTF_LAYOUT), "layout_feature_missing"))
		goto out;
	if (!ASSERT_TRUE(kernel_supports(skel->obj, FEAT_BTF_FUNC), "other_feature_allowed"))
		goto out;

	sanitized = bpf_object__sanitize_btf(skel->obj, orig);
	if (!ASSERT_OK_PTR(sanitized, "bpf_object__sanitize_btf"))
		goto out;

	raw = btf__raw_data(sanitized, &raw_sz);
	if (!ASSERT_OK_PTR(raw, "btf__raw_data_sanitized"))
		goto out;
	hdr = (struct btf_header *)raw;
	ASSERT_EQ(hdr->layout_off, 0, "layout_off_zero");
	ASSERT_EQ(hdr->layout_len, 0, "layout_len_zero");
	ASSERT_EQ(hdr->str_off, TYPE_LEN, "strs_after_types");
	ASSERT_EQ(hdr->str_len, STR_LEN, "strs_len_unchanged");
	ASSERT_EQ(raw_sz, hdr->hdr_len + hdr->type_len + hdr->str_len, "btf_raw_sz_reduced");
out:
	/* This will free the cache we allocated above */
	kfree_skb__destroy(skel);
	btf__free(sanitized);
	btf__free(orig);
}
