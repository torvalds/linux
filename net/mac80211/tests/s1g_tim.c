// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for S1G TIM PVB decoding. This test suite covers
 * IEEE80211-2024 Annex L figures 8, 9, 10, 12, 13, 14. ADE mode
 * is not covered as it is an optional encoding format and is not
 * currently supported by mac80211.
 *
 * Copyright (C) 2025 Morse Micro
 */
#include <linux/ieee80211.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#define MAX_AID 128

#define BC(enc_mode, inverse, blk_off)                          \
	((((blk_off) & 0x1f) << 3) | ((inverse) ? BIT(2) : 0) | \
	 ((enc_mode) & 0x3))

static void byte_to_bitstr(u8 v, char *out)
{
	for (int b = 7; b >= 0; b--)
		*out++ = (v & BIT(b)) ? '1' : '0';
	*out = '\0';
}

static void dump_tim_bits(struct kunit *test,
			  const struct ieee80211_tim_ie *tim, u8 tim_len)
{
	const u8 *ptr = tim->virtual_map;
	const u8 *end = (const u8 *)tim + tim_len;
	unsigned int oct = 1;
	unsigned int blk = 0;
	char bits[9];

	while (ptr < end) {
		u8 ctrl = *ptr++;
		u8 mode = ctrl & 0x03;
		bool inverse = ctrl & BIT(2);
		u8 blk_off = ctrl >> 3;

		kunit_info(
			test, "Block %u (ENC=%s, blk_off=%u, inverse=%u)", blk,
			(mode == IEEE80211_S1G_TIM_ENC_MODE_BLOCK)  ? "BLOCK" :
			(mode == IEEE80211_S1G_TIM_ENC_MODE_SINGLE) ? "SINGLE" :
								      "OLB",
			blk_off, inverse);

		byte_to_bitstr(ctrl, bits);
		kunit_info(test, "  octet %2u (ctrl)    : %s (0x%02x)", oct,
			   bits, ctrl);
		++oct;

		switch (mode) {
		case IEEE80211_S1G_TIM_ENC_MODE_BLOCK: {
			u8 blkmap = *ptr++;

			byte_to_bitstr(blkmap, bits);
			kunit_info(test, "  octet %2u (blk-map) : %s (0x%02x)",
				   oct, bits, blkmap);
			++oct;

			for (u8 sb = 0; sb < 8; sb++) {
				if (!(blkmap & BIT(sb)))
					continue;
				u8 sub = *ptr++;

				byte_to_bitstr(sub, bits);
				kunit_info(
					test,
					"  octet %2u (SB %2u)   : %s (0x%02x)",
					oct, sb, bits, sub);
				++oct;
			}
			break;
		}
		case IEEE80211_S1G_TIM_ENC_MODE_SINGLE: {
			u8 single = *ptr++;

			byte_to_bitstr(single, bits);
			kunit_info(test, "  octet %2u (single)  : %s (0x%02x)",
				   oct, bits, single);
			++oct;
			break;
		}
		case IEEE80211_S1G_TIM_ENC_MODE_OLB: {
			u8 len = *ptr++;

			byte_to_bitstr(len, bits);
			kunit_info(test, "  octet %2u (len=%2u)  : %s (0x%02x)",
				   oct, len, bits, len);
			++oct;

			for (u8 i = 0; i < len && ptr < end; i++) {
				u8 sub = *ptr++;

				byte_to_bitstr(sub, bits);
				kunit_info(
					test,
					"  octet %2u (SB %2u)   : %s (0x%02x)",
					oct, i, bits, sub);
				++oct;
			}
			break;
		}
		default:
			kunit_info(test, "  ** unknown encoding 0x%x **", mode);
			return;
		}
		blk++;
	}
}

static void tim_push(u8 **p, u8 v)
{
	*(*p)++ = v;
}

static void tim_begin(struct ieee80211_tim_ie *tim, u8 **p)
{
	tim->dtim_count = 0;
	tim->dtim_period = 1;
	tim->bitmap_ctrl = 0;
	*p = tim->virtual_map;
}

static u8 tim_end(struct ieee80211_tim_ie *tim, u8 *tail)
{
	return tail - (u8 *)tim;
}

static void pvb_add_block_bitmap(u8 **p, u8 blk_off, bool inverse, u8 blk_bmap,
				 const u8 *subblocks)
{
	u8 enc = IEEE80211_S1G_TIM_ENC_MODE_BLOCK;
	u8 n = hweight8(blk_bmap);

	tim_push(p, BC(enc, inverse, blk_off));
	tim_push(p, blk_bmap);

	for (u8 i = 0; i < n; i++)
		tim_push(p, subblocks[i]);
}

static void pvb_add_single_aid(u8 **p, u8 blk_off, bool inverse, u8 single6)
{
	u8 enc = IEEE80211_S1G_TIM_ENC_MODE_SINGLE;

	tim_push(p, BC(enc, inverse, blk_off));
	tim_push(p, single6 & GENMASK(5, 0));
}

static void pvb_add_olb(u8 **p, u8 blk_off, bool inverse, const u8 *subblocks,
			u8 len)
{
	u8 enc = IEEE80211_S1G_TIM_ENC_MODE_OLB;

	tim_push(p, BC(enc, inverse, blk_off));
	tim_push(p, len);
	for (u8 i = 0; i < len; i++)
		tim_push(p, subblocks[i]);
}

static void check_all_aids(struct kunit *test,
			   const struct ieee80211_tim_ie *tim, u8 tim_len,
			   const unsigned long *expected)
{
	for (u16 aid = 1; aid <= MAX_AID; aid++) {
		bool want = test_bit(aid, expected);
		bool got = ieee80211_s1g_check_tim(tim, tim_len, aid);

		KUNIT_ASSERT_EQ_MSG(test, got, want,
				    "AID %u mismatch (got=%d want=%d)", aid,
				    got, want);
	}
}

static void fill_bitmap(unsigned long *bm, const u16 *list, size_t n)
{
	size_t i;

	bitmap_zero(bm, MAX_AID + 1);
	for (i = 0; i < n; i++)
		__set_bit(list[i], bm);
}

static void fill_bitmap_inverse(unsigned long *bm, u16 max_aid,
				const u16 *except, size_t n_except)
{
	bitmap_zero(bm, MAX_AID + 1);
	for (u16 aid = 1; aid <= max_aid; aid++)
		__set_bit(aid, bm);

	for (size_t i = 0; i < n_except; i++)
		if (except[i] <= max_aid)
			__clear_bit(except[i], bm);
}

static void s1g_tim_block_test(struct kunit *test)
{
	u8 buf[256] = {};
	struct ieee80211_tim_ie *tim = (void *)buf;
	u8 *p, tim_len;
	static const u8 subblocks[] = {
		0x42, /* SB m=0: AIDs 1,6 */
		0xA0, /* SB m=2: AIDs 21,23 */
	};
	u8 blk_bmap = 0x05; /* bits 0 and 2 set */
	bool inverse = false;
	static const u16 set_list[] = { 1, 6, 21, 23 };
	DECLARE_BITMAP(exp, MAX_AID + 1);

	tim_begin(tim, &p);
	pvb_add_block_bitmap(&p, 0, inverse, blk_bmap, subblocks);
	tim_len = tim_end(tim, p);

	fill_bitmap(exp, set_list, ARRAY_SIZE(set_list));

	dump_tim_bits(test, tim, tim_len);
	check_all_aids(test, tim, tim_len, exp);
}

static void s1g_tim_single_test(struct kunit *test)
{
	u8 buf[256] = {};
	struct ieee80211_tim_ie *tim = (void *)buf;
	u8 *p, tim_len;
	bool inverse = false;
	u8 blk_off = 0;
	u8 single6 = 0x1f; /* 31 */
	static const u16 set_list[] = { 31 };
	DECLARE_BITMAP(exp, MAX_AID + 1);

	tim_begin(tim, &p);
	pvb_add_single_aid(&p, blk_off, inverse, single6);
	tim_len = tim_end(tim, p);

	fill_bitmap(exp, set_list, ARRAY_SIZE(set_list));

	dump_tim_bits(test, tim, tim_len);
	check_all_aids(test, tim, tim_len, exp);
}

static void s1g_tim_olb_test(struct kunit *test)
{
	u8 buf[256] = {};
	struct ieee80211_tim_ie *tim = (void *)buf;
	u8 *p, tim_len;
	bool inverse = false;
	u8 blk_off = 0;
	static const u16 set_list[] = { 1,  6,	13, 15, 17, 22, 29, 31, 33,
					38, 45, 47, 49, 54, 61, 63, 65, 70 };
	static const u8 subblocks[] = { 0x42, 0xA0, 0x42, 0xA0, 0x42,
					0xA0, 0x42, 0xA0, 0x42 };
	u8 len = ARRAY_SIZE(subblocks);
	DECLARE_BITMAP(exp, MAX_AID + 1);

	tim_begin(tim, &p);
	pvb_add_olb(&p, blk_off, inverse, subblocks, len);
	tim_len = tim_end(tim, p);

	fill_bitmap(exp, set_list, ARRAY_SIZE(set_list));

	dump_tim_bits(test, tim, tim_len);
	check_all_aids(test, tim, tim_len, exp);
}

static void s1g_tim_inverse_block_test(struct kunit *test)
{
	u8 buf[256] = {};
	struct ieee80211_tim_ie *tim = (void *)buf;
	u8 *p, tim_len;
	/* Same sub-block content as Figure L-8, but inverse = true */
	static const u8 subblocks[] = {
		0x42, /* SB m=0: AIDs 1,6 */
		0xA0, /* SB m=2: AIDs 21,23 */
	};
	u8 blk_bmap = 0x05;
	bool inverse = true;
	/*  All AIDs except 1,6,21,23 are set */
	static const u16 except[] = { 1, 6, 21, 23 };
	DECLARE_BITMAP(exp, MAX_AID + 1);

	tim_begin(tim, &p);
	pvb_add_block_bitmap(&p, 0, inverse, blk_bmap, subblocks);
	tim_len = tim_end(tim, p);

	fill_bitmap_inverse(exp, 63, except, ARRAY_SIZE(except));

	dump_tim_bits(test, tim, tim_len);
	check_all_aids(test, tim, tim_len, exp);
}

static void s1g_tim_inverse_single_test(struct kunit *test)
{
	u8 buf[256] = {};
	struct ieee80211_tim_ie *tim = (void *)buf;
	u8 *p, tim_len;
	bool inverse = true;
	u8 blk_off = 0;
	u8 single6 = 0x1f; /* 31 */
	/*  All AIDs except 31 are set */
	static const u16 except[] = { 31 };
	DECLARE_BITMAP(exp, MAX_AID + 1);

	tim_begin(tim, &p);
	pvb_add_single_aid(&p, blk_off, inverse, single6);
	tim_len = tim_end(tim, p);

	fill_bitmap_inverse(exp, 63, except, ARRAY_SIZE(except));

	dump_tim_bits(test, tim, tim_len);
	check_all_aids(test, tim, tim_len, exp);
}

static void s1g_tim_inverse_olb_test(struct kunit *test)
{
	u8 buf[256] = {};
	struct ieee80211_tim_ie *tim = (void *)buf;
	u8 *p, tim_len;
	bool inverse = true;
	u8 blk_off = 0, len;
	/*  All AIDs except the list below are set */
	static const u16 except[] = { 1,  6,  13, 15, 17, 22, 29, 31, 33,
				      38, 45, 47, 49, 54, 61, 63, 65, 70 };
	static const u8 subblocks[] = { 0x42, 0xA0, 0x42, 0xA0, 0x42,
					0xA0, 0x42, 0xA0, 0x42 };
	len = ARRAY_SIZE(subblocks);
	DECLARE_BITMAP(exp, MAX_AID + 1);

	tim_begin(tim, &p);
	pvb_add_olb(&p, blk_off, inverse, subblocks, len);
	tim_len = tim_end(tim, p);

	fill_bitmap_inverse(exp, 127, except, ARRAY_SIZE(except));

	dump_tim_bits(test, tim, tim_len);
	check_all_aids(test, tim, tim_len, exp);
}

static struct kunit_case s1g_tim_test_cases[] = {
	KUNIT_CASE(s1g_tim_block_test),
	KUNIT_CASE(s1g_tim_single_test),
	KUNIT_CASE(s1g_tim_olb_test),
	KUNIT_CASE(s1g_tim_inverse_block_test),
	KUNIT_CASE(s1g_tim_inverse_single_test),
	KUNIT_CASE(s1g_tim_inverse_olb_test),
	{}
};

static struct kunit_suite s1g_tim = {
	.name = "mac80211-s1g-tim",
	.test_cases = s1g_tim_test_cases,
};

kunit_test_suite(s1g_tim);
