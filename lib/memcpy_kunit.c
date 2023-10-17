// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for memcpy(), memmove(), and memset().
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

struct some_bytes {
	union {
		u8 data[32];
		struct {
			u32 one;
			u16 two;
			u8  three;
			/* 1 byte hole */
			u32 four[4];
		};
	};
};

#define check(instance, v) do {	\
	BUILD_BUG_ON(sizeof(instance.data) != 32);	\
	for (size_t i = 0; i < sizeof(instance.data); i++) {	\
		KUNIT_ASSERT_EQ_MSG(test, instance.data[i], v, \
			"line %d: '%s' not initialized to 0x%02x @ %d (saw 0x%02x)\n", \
			__LINE__, #instance, v, i, instance.data[i]);	\
	}	\
} while (0)

#define compare(name, one, two) do { \
	BUILD_BUG_ON(sizeof(one) != sizeof(two)); \
	for (size_t i = 0; i < sizeof(one); i++) {	\
		KUNIT_EXPECT_EQ_MSG(test, one.data[i], two.data[i], \
			"line %d: %s.data[%d] (0x%02x) != %s.data[%d] (0x%02x)\n", \
			__LINE__, #one, i, one.data[i], #two, i, two.data[i]); \
	}	\
	kunit_info(test, "ok: " TEST_OP "() " name "\n");	\
} while (0)

static void memcpy_test(struct kunit *test)
{
#define TEST_OP "memcpy"
	struct some_bytes control = {
		.data = { 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			},
	};
	struct some_bytes zero = { };
	struct some_bytes middle = {
		.data = { 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			},
	};
	struct some_bytes three = {
		.data = { 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			},
	};
	struct some_bytes dest = { };
	int count;
	u8 *ptr;

	/* Verify static initializers. */
	check(control, 0x20);
	check(zero, 0);
	compare("static initializers", dest, zero);

	/* Verify assignment. */
	dest = control;
	compare("direct assignment", dest, control);

	/* Verify complete overwrite. */
	memcpy(dest.data, zero.data, sizeof(dest.data));
	compare("complete overwrite", dest, zero);

	/* Verify middle overwrite. */
	dest = control;
	memcpy(dest.data + 12, zero.data, 7);
	compare("middle overwrite", dest, middle);

	/* Verify argument side-effects aren't repeated. */
	dest = control;
	ptr = dest.data;
	count = 1;
	memcpy(ptr++, zero.data, count++);
	ptr += 8;
	memcpy(ptr++, zero.data, count++);
	compare("argument side-effects", dest, three);
#undef TEST_OP
}

static unsigned char larger_array [2048];

static void memmove_test(struct kunit *test)
{
#define TEST_OP "memmove"
	struct some_bytes control = {
		.data = { 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			},
	};
	struct some_bytes zero = { };
	struct some_bytes middle = {
		.data = { 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			},
	};
	struct some_bytes five = {
		.data = { 0x00, 0x00, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x00, 0x00, 0x00, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			},
	};
	struct some_bytes overlap = {
		.data = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			},
	};
	struct some_bytes overlap_expected = {
		.data = { 0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04, 0x07,
			  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			  0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
			},
	};
	struct some_bytes dest = { };
	int count;
	u8 *ptr;

	/* Verify static initializers. */
	check(control, 0x99);
	check(zero, 0);
	compare("static initializers", zero, dest);

	/* Verify assignment. */
	dest = control;
	compare("direct assignment", dest, control);

	/* Verify complete overwrite. */
	memmove(dest.data, zero.data, sizeof(dest.data));
	compare("complete overwrite", dest, zero);

	/* Verify middle overwrite. */
	dest = control;
	memmove(dest.data + 12, zero.data, 7);
	compare("middle overwrite", dest, middle);

	/* Verify argument side-effects aren't repeated. */
	dest = control;
	ptr = dest.data;
	count = 2;
	memmove(ptr++, zero.data, count++);
	ptr += 9;
	memmove(ptr++, zero.data, count++);
	compare("argument side-effects", dest, five);

	/* Verify overlapping overwrite is correct. */
	ptr = &overlap.data[2];
	memmove(ptr, overlap.data, 5);
	compare("overlapping write", overlap, overlap_expected);

	/* Verify larger overlapping moves. */
	larger_array[256] = 0xAAu;
	/*
	 * Test a backwards overlapping memmove first. 256 and 1024 are
	 * important for i386 to use rep movsl.
	 */
	memmove(larger_array, larger_array + 256, 1024);
	KUNIT_ASSERT_EQ(test, larger_array[0], 0xAAu);
	KUNIT_ASSERT_EQ(test, larger_array[256], 0x00);
	KUNIT_ASSERT_NULL(test,
		memchr(larger_array + 1, 0xaa, ARRAY_SIZE(larger_array) - 1));
	/* Test a forwards overlapping memmove. */
	larger_array[0] = 0xBBu;
	memmove(larger_array + 256, larger_array, 1024);
	KUNIT_ASSERT_EQ(test, larger_array[0], 0xBBu);
	KUNIT_ASSERT_EQ(test, larger_array[256], 0xBBu);
	KUNIT_ASSERT_NULL(test, memchr(larger_array + 1, 0xBBu, 256 - 1));
	KUNIT_ASSERT_NULL(test,
		memchr(larger_array + 257, 0xBBu, ARRAY_SIZE(larger_array) - 257));
#undef TEST_OP
}

static void memset_test(struct kunit *test)
{
#define TEST_OP "memset"
	struct some_bytes control = {
		.data = { 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			  0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			  0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			  0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			},
	};
	struct some_bytes complete = {
		.data = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			},
	};
	struct some_bytes middle = {
		.data = { 0x30, 0x30, 0x30, 0x30, 0x31, 0x31, 0x31, 0x31,
			  0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
			  0x31, 0x31, 0x31, 0x31, 0x30, 0x30, 0x30, 0x30,
			  0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			},
	};
	struct some_bytes three = {
		.data = { 0x60, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			  0x30, 0x61, 0x61, 0x30, 0x30, 0x30, 0x30, 0x30,
			  0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			  0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			},
	};
	struct some_bytes after = {
		.data = { 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x72,
			  0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
			  0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
			  0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
			},
	};
	struct some_bytes startat = {
		.data = { 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			  0x79, 0x79, 0x79, 0x79, 0x79, 0x79, 0x79, 0x79,
			  0x79, 0x79, 0x79, 0x79, 0x79, 0x79, 0x79, 0x79,
			  0x79, 0x79, 0x79, 0x79, 0x79, 0x79, 0x79, 0x79,
			},
	};
	struct some_bytes dest = { };
	int count, value;
	u8 *ptr;

	/* Verify static initializers. */
	check(control, 0x30);
	check(dest, 0);

	/* Verify assignment. */
	dest = control;
	compare("direct assignment", dest, control);

	/* Verify complete overwrite. */
	memset(dest.data, 0xff, sizeof(dest.data));
	compare("complete overwrite", dest, complete);

	/* Verify middle overwrite. */
	dest = control;
	memset(dest.data + 4, 0x31, 16);
	compare("middle overwrite", dest, middle);

	/* Verify argument side-effects aren't repeated. */
	dest = control;
	ptr = dest.data;
	value = 0x60;
	count = 1;
	memset(ptr++, value++, count++);
	ptr += 8;
	memset(ptr++, value++, count++);
	compare("argument side-effects", dest, three);

	/* Verify memset_after() */
	dest = control;
	memset_after(&dest, 0x72, three);
	compare("memset_after()", dest, after);

	/* Verify memset_startat() */
	dest = control;
	memset_startat(&dest, 0x79, four);
	compare("memset_startat()", dest, startat);
#undef TEST_OP
}

static u8 large_src[1024];
static u8 large_dst[2048];
static const u8 large_zero[2048];

static void set_random_nonzero(struct kunit *test, u8 *byte)
{
	int failed_rng = 0;

	while (*byte == 0) {
		get_random_bytes(byte, 1);
		KUNIT_ASSERT_LT_MSG(test, failed_rng++, 100,
				    "Is the RNG broken?");
	}
}

static void init_large(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_MEMCPY_SLOW_KUNIT_TEST))
		kunit_skip(test, "Slow test skipped. Enable with CONFIG_MEMCPY_SLOW_KUNIT_TEST=y");

	/* Get many bit patterns. */
	get_random_bytes(large_src, ARRAY_SIZE(large_src));

	/* Make sure we have non-zero edges. */
	set_random_nonzero(test, &large_src[0]);
	set_random_nonzero(test, &large_src[ARRAY_SIZE(large_src) - 1]);

	/* Explicitly zero the entire destination. */
	memset(large_dst, 0, ARRAY_SIZE(large_dst));
}

/*
 * Instead of an indirect function call for "copy" or a giant macro,
 * use a bool to pick memcpy or memmove.
 */
static void copy_large_test(struct kunit *test, bool use_memmove)
{
	init_large(test);

	/* Copy a growing number of non-overlapping bytes ... */
	for (int bytes = 1; bytes <= ARRAY_SIZE(large_src); bytes++) {
		/* Over a shifting destination window ... */
		for (int offset = 0; offset < ARRAY_SIZE(large_src); offset++) {
			int right_zero_pos = offset + bytes;
			int right_zero_size = ARRAY_SIZE(large_dst) - right_zero_pos;

			/* Copy! */
			if (use_memmove)
				memmove(large_dst + offset, large_src, bytes);
			else
				memcpy(large_dst + offset, large_src, bytes);

			/* Did we touch anything before the copy area? */
			KUNIT_ASSERT_EQ_MSG(test,
				memcmp(large_dst, large_zero, offset), 0,
				"with size %d at offset %d", bytes, offset);
			/* Did we touch anything after the copy area? */
			KUNIT_ASSERT_EQ_MSG(test,
				memcmp(&large_dst[right_zero_pos], large_zero, right_zero_size), 0,
				"with size %d at offset %d", bytes, offset);

			/* Are we byte-for-byte exact across the copy? */
			KUNIT_ASSERT_EQ_MSG(test,
				memcmp(large_dst + offset, large_src, bytes), 0,
				"with size %d at offset %d", bytes, offset);

			/* Zero out what we copied for the next cycle. */
			memset(large_dst + offset, 0, bytes);
		}
		/* Avoid stall warnings if this loop gets slow. */
		cond_resched();
	}
}

static void memcpy_large_test(struct kunit *test)
{
	copy_large_test(test, false);
}

static void memmove_large_test(struct kunit *test)
{
	copy_large_test(test, true);
}

/*
 * On the assumption that boundary conditions are going to be the most
 * sensitive, instead of taking a full step (inc) each iteration,
 * take single index steps for at least the first "inc"-many indexes
 * from the "start" and at least the last "inc"-many indexes before
 * the "end". When in the middle, take full "inc"-wide steps. For
 * example, calling next_step(idx, 1, 15, 3) with idx starting at 0
 * would see the following pattern: 1 2 3 4 7 10 11 12 13 14 15.
 */
static int next_step(int idx, int start, int end, int inc)
{
	start += inc;
	end -= inc;

	if (idx < start || idx + inc > end)
		inc = 1;
	return idx + inc;
}

static void inner_loop(struct kunit *test, int bytes, int d_off, int s_off)
{
	int left_zero_pos, left_zero_size;
	int right_zero_pos, right_zero_size;
	int src_pos, src_orig_pos, src_size;
	int pos;

	/* Place the source in the destination buffer. */
	memcpy(&large_dst[s_off], large_src, bytes);

	/* Copy to destination offset. */
	memmove(&large_dst[d_off], &large_dst[s_off], bytes);

	/* Make sure destination entirely matches. */
	KUNIT_ASSERT_EQ_MSG(test, memcmp(&large_dst[d_off], large_src, bytes), 0,
		"with size %d at src offset %d and dest offset %d",
		bytes, s_off, d_off);

	/* Calculate the expected zero spans. */
	if (s_off < d_off) {
		left_zero_pos = 0;
		left_zero_size = s_off;

		right_zero_pos = d_off + bytes;
		right_zero_size = ARRAY_SIZE(large_dst) - right_zero_pos;

		src_pos = s_off;
		src_orig_pos = 0;
		src_size = d_off - s_off;
	} else {
		left_zero_pos = 0;
		left_zero_size = d_off;

		right_zero_pos = s_off + bytes;
		right_zero_size = ARRAY_SIZE(large_dst) - right_zero_pos;

		src_pos = d_off + bytes;
		src_orig_pos = src_pos - s_off;
		src_size = right_zero_pos - src_pos;
	}

	/* Check non-overlapping source is unchanged.*/
	KUNIT_ASSERT_EQ_MSG(test,
		memcmp(&large_dst[src_pos], &large_src[src_orig_pos], src_size), 0,
		"with size %d at src offset %d and dest offset %d",
		bytes, s_off, d_off);

	/* Check leading buffer contents are zero. */
	KUNIT_ASSERT_EQ_MSG(test,
		memcmp(&large_dst[left_zero_pos], large_zero, left_zero_size), 0,
		"with size %d at src offset %d and dest offset %d",
		bytes, s_off, d_off);
	/* Check trailing buffer contents are zero. */
	KUNIT_ASSERT_EQ_MSG(test,
		memcmp(&large_dst[right_zero_pos], large_zero, right_zero_size), 0,
		"with size %d at src offset %d and dest offset %d",
		bytes, s_off, d_off);

	/* Zero out everything not already zeroed.*/
	pos = left_zero_pos + left_zero_size;
	memset(&large_dst[pos], 0, right_zero_pos - pos);
}

static void memmove_overlap_test(struct kunit *test)
{
	/*
	 * Running all possible offset and overlap combinations takes a
	 * very long time. Instead, only check up to 128 bytes offset
	 * into the destination buffer (which should result in crossing
	 * cachelines), with a step size of 1 through 7 to try to skip some
	 * redundancy.
	 */
	static const int offset_max = 128; /* less than ARRAY_SIZE(large_src); */
	static const int bytes_step = 7;
	static const int window_step = 7;

	static const int bytes_start = 1;
	static const int bytes_end = ARRAY_SIZE(large_src) + 1;

	init_large(test);

	/* Copy a growing number of overlapping bytes ... */
	for (int bytes = bytes_start; bytes < bytes_end;
	     bytes = next_step(bytes, bytes_start, bytes_end, bytes_step)) {

		/* Over a shifting destination window ... */
		for (int d_off = 0; d_off < offset_max; d_off++) {
			int s_start = max(d_off - bytes, 0);
			int s_end = min_t(int, d_off + bytes, ARRAY_SIZE(large_src));

			/* Over a shifting source window ... */
			for (int s_off = s_start; s_off < s_end;
			     s_off = next_step(s_off, s_start, s_end, window_step))
				inner_loop(test, bytes, d_off, s_off);

			/* Avoid stall warnings. */
			cond_resched();
		}
	}
}

static void strtomem_test(struct kunit *test)
{
	static const char input[sizeof(unsigned long)] = "hi";
	static const char truncate[] = "this is too long";
	struct {
		unsigned long canary1;
		unsigned char output[sizeof(unsigned long)] __nonstring;
		unsigned long canary2;
	} wrap;

	memset(&wrap, 0xFF, sizeof(wrap));
	KUNIT_EXPECT_EQ_MSG(test, wrap.canary1, ULONG_MAX,
			    "bad initial canary value");
	KUNIT_EXPECT_EQ_MSG(test, wrap.canary2, ULONG_MAX,
			    "bad initial canary value");

	/* Check unpadded copy leaves surroundings untouched. */
	strtomem(wrap.output, input);
	KUNIT_EXPECT_EQ(test, wrap.canary1, ULONG_MAX);
	KUNIT_EXPECT_EQ(test, wrap.output[0], input[0]);
	KUNIT_EXPECT_EQ(test, wrap.output[1], input[1]);
	for (size_t i = 2; i < sizeof(wrap.output); i++)
		KUNIT_EXPECT_EQ(test, wrap.output[i], 0xFF);
	KUNIT_EXPECT_EQ(test, wrap.canary2, ULONG_MAX);

	/* Check truncated copy leaves surroundings untouched. */
	memset(&wrap, 0xFF, sizeof(wrap));
	strtomem(wrap.output, truncate);
	KUNIT_EXPECT_EQ(test, wrap.canary1, ULONG_MAX);
	for (size_t i = 0; i < sizeof(wrap.output); i++)
		KUNIT_EXPECT_EQ(test, wrap.output[i], truncate[i]);
	KUNIT_EXPECT_EQ(test, wrap.canary2, ULONG_MAX);

	/* Check padded copy leaves only string padded. */
	memset(&wrap, 0xFF, sizeof(wrap));
	strtomem_pad(wrap.output, input, 0xAA);
	KUNIT_EXPECT_EQ(test, wrap.canary1, ULONG_MAX);
	KUNIT_EXPECT_EQ(test, wrap.output[0], input[0]);
	KUNIT_EXPECT_EQ(test, wrap.output[1], input[1]);
	for (size_t i = 2; i < sizeof(wrap.output); i++)
		KUNIT_EXPECT_EQ(test, wrap.output[i], 0xAA);
	KUNIT_EXPECT_EQ(test, wrap.canary2, ULONG_MAX);

	/* Check truncated padded copy has no padding. */
	memset(&wrap, 0xFF, sizeof(wrap));
	strtomem(wrap.output, truncate);
	KUNIT_EXPECT_EQ(test, wrap.canary1, ULONG_MAX);
	for (size_t i = 0; i < sizeof(wrap.output); i++)
		KUNIT_EXPECT_EQ(test, wrap.output[i], truncate[i]);
	KUNIT_EXPECT_EQ(test, wrap.canary2, ULONG_MAX);
}

static struct kunit_case memcpy_test_cases[] = {
	KUNIT_CASE(memset_test),
	KUNIT_CASE(memcpy_test),
	KUNIT_CASE_SLOW(memcpy_large_test),
	KUNIT_CASE_SLOW(memmove_test),
	KUNIT_CASE_SLOW(memmove_large_test),
	KUNIT_CASE_SLOW(memmove_overlap_test),
	KUNIT_CASE(strtomem_test),
	{}
};

static struct kunit_suite memcpy_test_suite = {
	.name = "memcpy",
	.test_cases = memcpy_test_cases,
};

kunit_test_suite(memcpy_test_suite);

MODULE_LICENSE("GPL");
