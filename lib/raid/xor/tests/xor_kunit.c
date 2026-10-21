// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Unit test the XOR library functions.
 *
 * Copyright 2024 Google LLC
 * Copyright 2026 Christoph Hellwig
 *
 * Based on the CRC tests by Eric Biggers <ebiggers@google.com>.
 */
#include <kunit/test.h>
#include <linux/prandom.h>
#include <linux/string_choices.h>
#include <linux/vmalloc.h>
#include <linux/raid/xor.h>

#define XOR_KUNIT_SEED			42
#define XOR_KUNIT_MAX_BYTES		16384
#define XOR_KUNIT_MAX_BUFFERS		64
#define XOR_KUNIT_NUM_TEST_ITERS	1000

static struct rnd_state rng;
static void *test_buffers[XOR_KUNIT_MAX_BUFFERS];
static void *test_dest;
static void *test_ref;
static size_t test_buflen;

static u32 rand32(void)
{
	return prandom_u32_state(&rng);
}

/* Reference implementation using dumb byte-wise XOR */
static void xor_ref(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes)
{
	unsigned int off, idx;
	u8 *d = dest;

	for (off = 0; off < bytes; off++) {
		for (idx = 0; idx < src_cnt; idx++) {
			u8 *src = srcs[idx];

			d[off] ^= src[off];
		}
	}
}

/* Generate a random length that is a multiple of 512. */
static unsigned int random_length(unsigned int max_length)
{
	return round_up((rand32() % max_length) + 1, 512);
}

/* Generate a random alignment that is a multiple of 64. */
static unsigned int random_alignment(unsigned int max_alignment)
{
	return ((rand32() % max_alignment) + 1) & ~63;
}

static void xor_generate_random_data(void)
{
	int i;

	prandom_bytes_state(&rng, test_dest, test_buflen);
	memcpy(test_ref, test_dest, test_buflen);
	for (i = 0; i < XOR_KUNIT_MAX_BUFFERS; i++)
		prandom_bytes_state(&rng, test_buffers[i], test_buflen);
}

/* Test that xor_gen gives the same result as a reference implementation. */
static void xor_test(struct kunit *test)
{
	void *aligned_buffers[XOR_KUNIT_MAX_BUFFERS];
	size_t i;

	for (i = 0; i < XOR_KUNIT_NUM_TEST_ITERS; i++) {
		unsigned int nr_buffers =
			(rand32() % XOR_KUNIT_MAX_BUFFERS) + 1;
		unsigned int len = random_length(XOR_KUNIT_MAX_BYTES);
		unsigned int max_alignment, align = 0;
		void *buffers;

		if (rand32() % 8 == 0)
			/* Refresh the data occasionally. */
			xor_generate_random_data();

		/*
		 * If we're not using the entire buffer size, inject randomized
		 * alignment into the buffer.
		 */
		max_alignment = XOR_KUNIT_MAX_BYTES - len;
		if (max_alignment == 0) {
			buffers = test_buffers;
		} else if (rand32() % 2 == 0) {
			/* Use random alignments mod 64 */
			int j;

			for (j = 0; j < nr_buffers; j++)
				aligned_buffers[j] = test_buffers[j] +
					random_alignment(max_alignment);
			buffers = aligned_buffers;
			align = random_alignment(max_alignment);
		} else {
			/* Go up to the guard page, to catch buffer overreads */
			int j;

			align = test_buflen - len;
			for (j = 0; j < nr_buffers; j++)
				aligned_buffers[j] = test_buffers[j] + align;
			buffers = aligned_buffers;
		}

		/*
		 * Compute the XOR, and verify that it equals the XOR computed
		 * by a simple byte-at-a-time reference implementation.
		 */
		xor_ref(test_ref + align, buffers, nr_buffers, len);
		xor_gen(test_dest + align, buffers, nr_buffers, len);
		KUNIT_EXPECT_MEMEQ_MSG(test, test_ref + align,
				test_dest + align, len,
				"Wrong result with buffers=%u, len=%u, unaligned=%s, at_end=%s",
				nr_buffers, len,
				str_yes_no(max_alignment),
				str_yes_no(align + len == test_buflen));
	}
}

static void xor_benchmark(struct kunit *test)
{
	static const unsigned int nr_to_test[] = {
		4, 5, 6, 7, 8, 10, 12, 15, 16, 32,
	};
	static const unsigned int len_to_test[] = {
		SZ_4K, SZ_16K,
	};
	unsigned int i, j, l;
	u64 t;

	if (!IS_ENABLED(CONFIG_XOR_BENCHMARK))
		kunit_skip(test, "not enabled");

	/* warm-up */
	for (i = 0; i < ARRAY_SIZE(nr_to_test); i++) {
		for (j = 0; j < ARRAY_SIZE(len_to_test); j++) {
			for (l = 0; l < 10; l++) {
				xor_gen(test_dest, test_buffers, nr_to_test[i],
						len_to_test[j]);
			}
		}
	}

	/*
	 * Preferably this would be a loop over len_to_test, but the kunit
	 * logging always adds a newline to each logged format string.
	 */
	static_assert(ARRAY_SIZE(len_to_test) == 2);
	kunit_info(test, "          \t%5u bytes\t%5u bytes\n",
			len_to_test[0], len_to_test[1]);

	for (i = 0; i < ARRAY_SIZE(nr_to_test); i++) {
		unsigned int nr = nr_to_test[i];
		u64 speed[ARRAY_SIZE(len_to_test)];

		KUNIT_ASSERT_LE(test, nr, XOR_KUNIT_MAX_BUFFERS);

		for (j = 0; j < ARRAY_SIZE(len_to_test); j++) {
			unsigned int len = len_to_test[j];
			const unsigned long num_iters = 1000;

			KUNIT_ASSERT_GT(test, len, 0);
			KUNIT_ASSERT_LE(test, len, XOR_KUNIT_MAX_BYTES);

			preempt_disable();
			t = ktime_get_ns();
			for (l = 0; l < num_iters; l++)
				xor_gen(test_dest, test_buffers, nr, len);
			t = max(ktime_get_ns() - t, 1);
			preempt_enable();

			speed[j] = div64_u64((u64)len * num_iters * nr, t);
		}

		static_assert(ARRAY_SIZE(len_to_test) == 2);
		kunit_info(test, "%3u disks:\t%5llu  GB/s\t%5llu  GB/s\n",
				nr, speed[0], speed[1]);
	}
}

static struct kunit_case xor_test_cases[] = {
	KUNIT_CASE(xor_test),
	KUNIT_CASE(xor_benchmark),
	{},
};

static int xor_suite_init(struct kunit_suite *suite)
{
	int i;

	/*
	 * Allocate the test buffer using vmalloc() with a page-aligned length
	 * so that it is immediately followed by a guard page.  This allows
	 * buffer overreads to be detected, even in assembly code.
	 */
	test_buflen = round_up(XOR_KUNIT_MAX_BYTES, PAGE_SIZE);
	test_ref = vmalloc(test_buflen);
	if (!test_ref)
		return -ENOMEM;
	test_dest = vmalloc(test_buflen);
	if (!test_dest)
		goto out_free_ref;
	for (i = 0; i < XOR_KUNIT_MAX_BUFFERS; i++) {
		test_buffers[i] = vmalloc(test_buflen);
		if (!test_buffers[i])
			goto out_free_buffers;
	}

	prandom_seed_state(&rng, XOR_KUNIT_SEED);
	xor_generate_random_data();
	return 0;

out_free_buffers:
	while (--i >= 0)
		vfree(test_buffers[i]);
	vfree(test_dest);
out_free_ref:
	vfree(test_ref);
	return -ENOMEM;
}

static void xor_suite_exit(struct kunit_suite *suite)
{
	int i;

	vfree(test_ref);
	vfree(test_dest);
	for (i = 0; i < XOR_KUNIT_MAX_BUFFERS; i++)
		vfree(test_buffers[i]);
}

static struct kunit_suite xor_test_suite = {
	.name		= "xor",
	.test_cases	= xor_test_cases,
	.suite_init	= xor_suite_init,
	.suite_exit	= xor_suite_exit,
};
kunit_test_suite(xor_test_suite);

MODULE_DESCRIPTION("Unit test for the XOR library functions");
MODULE_LICENSE("GPL");
