// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Unit tests and benchmarks for the CRC library functions
 *
 * Copyright 2024 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */
#include <kunit/run-in-irq-context.h>
#include <kunit/test.h>
#include <linux/crc7.h>
#include <linux/crc16.h>
#include <linux/crc-t10dif.h>
#include <linux/crc32.h>
#include <linux/crc32c.h>
#include <linux/crc64.h>
#include <linux/prandom.h>
#include <linux/vmalloc.h>

#define CRC_KUNIT_SEED			42
#define CRC_KUNIT_MAX_LEN		16384
#define CRC_KUNIT_NUM_TEST_ITERS	1000

static struct rnd_state rng;
static u8 *test_buffer;
static size_t test_buflen;

/**
 * struct crc_variant - describes a CRC variant
 * @bits: Number of bits in the CRC, 1 <= @bits <= 64.
 * @le: true if it's a "little endian" CRC (reversed mapping between bits and
 *	polynomial coefficients in each byte), false if it's a "big endian" CRC
 *	(natural mapping between bits and polynomial coefficients in each byte)
 * @poly: The generator polynomial with the highest-order term omitted.
 *	  Bit-reversed if @le is true.
 * @func: The function to compute a CRC.  The type signature uses u64 so that it
 *	  can fit any CRC up to CRC-64.  The CRC is passed in, and is expected
 *	  to be returned in, the least significant bits of the u64.  The
 *	  function is expected to *not* invert the CRC at the beginning and end.
 */
struct crc_variant {
	int bits;
	bool le;
	u64 poly;
	u64 (*func)(u64 crc, const u8 *p, size_t len);
};

static u32 rand32(void)
{
	return prandom_u32_state(&rng);
}

static u64 rand64(void)
{
	u32 n = rand32();

	return ((u64)n << 32) | rand32();
}

static u64 crc_mask(const struct crc_variant *v)
{
	return (u64)-1 >> (64 - v->bits);
}

/* Reference implementation of any CRC variant */
static u64 crc_ref(const struct crc_variant *v,
		   u64 crc, const u8 *p, size_t len)
{
	size_t i, j;

	for (i = 0; i < len; i++) {
		for (j = 0; j < 8; j++) {
			if (v->le) {
				crc ^= (p[i] >> j) & 1;
				crc = (crc >> 1) ^ ((crc & 1) ? v->poly : 0);
			} else {
				crc ^= (u64)((p[i] >> (7 - j)) & 1) <<
				       (v->bits - 1);
				if (crc & (1ULL << (v->bits - 1)))
					crc = ((crc << 1) ^ v->poly) &
					      crc_mask(v);
				else
					crc <<= 1;
			}
		}
	}
	return crc;
}

static int crc_suite_init(struct kunit_suite *suite)
{
	/*
	 * Allocate the test buffer using vmalloc() with a page-aligned length
	 * so that it is immediately followed by a guard page.  This allows
	 * buffer overreads to be detected, even in assembly code.
	 */
	test_buflen = round_up(CRC_KUNIT_MAX_LEN, PAGE_SIZE);
	test_buffer = vmalloc(test_buflen);
	if (!test_buffer)
		return -ENOMEM;

	prandom_seed_state(&rng, CRC_KUNIT_SEED);
	prandom_bytes_state(&rng, test_buffer, test_buflen);
	return 0;
}

static void crc_suite_exit(struct kunit_suite *suite)
{
	vfree(test_buffer);
	test_buffer = NULL;
}

/* Generate a random initial CRC. */
static u64 generate_random_initial_crc(const struct crc_variant *v)
{
	switch (rand32() % 4) {
	case 0:
		return 0;
	case 1:
		return crc_mask(v); /* All 1 bits */
	default:
		return rand64() & crc_mask(v);
	}
}

/* Generate a random length, preferring small lengths. */
static size_t generate_random_length(size_t max_length)
{
	size_t len;

	switch (rand32() % 3) {
	case 0:
		len = rand32() % 128;
		break;
	case 1:
		len = rand32() % 3072;
		break;
	default:
		len = rand32();
		break;
	}
	return len % (max_length + 1);
}

#define IRQ_TEST_DATA_LEN 512
#define IRQ_TEST_NUM_BUFFERS 3 /* matches max concurrency level */

struct crc_irq_test_state {
	const struct crc_variant *v;
	u64 initial_crc;
	u64 expected_crcs[IRQ_TEST_NUM_BUFFERS];
	atomic_t seqno;
};

/*
 * Compute the CRC of one of the test messages and verify that it matches the
 * expected CRC from @state->expected_crcs.  To increase the chance of detecting
 * problems, cycle through multiple messages.
 */
static bool crc_irq_test_func(void *state_)
{
	struct crc_irq_test_state *state = state_;
	const struct crc_variant *v = state->v;
	u32 i = (u32)atomic_inc_return(&state->seqno) % IRQ_TEST_NUM_BUFFERS;
	u64 actual_crc = v->func(state->initial_crc,
				 &test_buffer[i * IRQ_TEST_DATA_LEN],
				 IRQ_TEST_DATA_LEN);

	return actual_crc == state->expected_crcs[i];
}

/*
 * Test that if CRCs are computed in task, softirq, and hardirq context
 * concurrently, then all results are as expected.
 */
static void crc_interrupt_context_test(struct kunit *test,
				       const struct crc_variant *v)
{
	struct crc_irq_test_state state = {
		.v = v,
		.initial_crc = generate_random_initial_crc(v),
	};

	for (int i = 0; i < IRQ_TEST_NUM_BUFFERS; i++) {
		state.expected_crcs[i] = crc_ref(
			v, state.initial_crc,
			&test_buffer[i * IRQ_TEST_DATA_LEN], IRQ_TEST_DATA_LEN);
	}

	kunit_run_irq_test(test, crc_irq_test_func, 100000, &state);
}

/* Test that v->func gives the same CRCs as a reference implementation. */
static void crc_test(struct kunit *test, const struct crc_variant *v)
{
	size_t i;

	for (i = 0; i < CRC_KUNIT_NUM_TEST_ITERS; i++) {
		u64 init_crc, expected_crc, actual_crc;
		size_t len, offset;

		init_crc = generate_random_initial_crc(v);
		len = generate_random_length(CRC_KUNIT_MAX_LEN);

		/* Generate a random offset. */
		if (rand32() % 2 == 0) {
			/* Use a random alignment mod 64 */
			offset = rand32() % 64;
			offset = min(offset, CRC_KUNIT_MAX_LEN - len);
		} else {
			/* Go up to the guard page, to catch buffer overreads */
			offset = test_buflen - len;
		}

		if (rand32() % 8 == 0)
			/* Refresh the data occasionally. */
			prandom_bytes_state(&rng, &test_buffer[offset], len);

		/*
		 * Compute the CRC, and verify that it equals the CRC computed
		 * by a simple bit-at-a-time reference implementation.
		 */
		expected_crc = crc_ref(v, init_crc, &test_buffer[offset], len);
		actual_crc = v->func(init_crc, &test_buffer[offset], len);
		KUNIT_EXPECT_EQ_MSG(test, expected_crc, actual_crc,
				    "Wrong result with len=%zu offset=%zu",
				    len, offset);
	}

	crc_interrupt_context_test(test, v);
}

static __always_inline void
crc_benchmark(struct kunit *test,
	      u64 (*crc_func)(u64 crc, const u8 *p, size_t len))
{
	static const size_t lens_to_test[] = {
		1, 16, 64, 127, 128, 200, 256, 511, 512, 1024, 3173, 4096, 16384,
	};
	size_t len, i, j, num_iters;
	/*
	 * The CRC value that this function computes in a series of calls to
	 * crc_func is never actually used, so use volatile to ensure that the
	 * computations are done as intended and don't all get optimized out.
	 */
	volatile u64 crc = 0;
	u64 t;

	if (!IS_ENABLED(CONFIG_CRC_BENCHMARK))
		kunit_skip(test, "not enabled");

	/* warm-up */
	for (i = 0; i < 10000000; i += CRC_KUNIT_MAX_LEN)
		crc = crc_func(crc, test_buffer, CRC_KUNIT_MAX_LEN);

	for (i = 0; i < ARRAY_SIZE(lens_to_test); i++) {
		len = lens_to_test[i];
		KUNIT_ASSERT_LE(test, len, CRC_KUNIT_MAX_LEN);
		num_iters = 10000000 / (len + 128);
		preempt_disable();
		t = ktime_get_ns();
		for (j = 0; j < num_iters; j++)
			crc = crc_func(crc, test_buffer, len);
		t = ktime_get_ns() - t;
		preempt_enable();
		kunit_info(test, "len=%zu: %llu MB/s\n",
			   len, div64_u64((u64)len * num_iters * 1000, t));
	}
}

/* crc7_be */

static u64 crc7_be_wrapper(u64 crc, const u8 *p, size_t len)
{
	/*
	 * crc7_be() left-aligns the 7-bit CRC in a u8, whereas the test wants a
	 * right-aligned CRC (in a u64).  Convert between the conventions.
	 */
	return crc7_be(crc << 1, p, len) >> 1;
}

static const struct crc_variant crc_variant_crc7_be = {
	.bits = 7,
	.poly = 0x9,
	.func = crc7_be_wrapper,
};

static void crc7_be_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc7_be);
}

static void crc7_be_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc7_be_wrapper);
}

/* crc16 */

static u64 crc16_wrapper(u64 crc, const u8 *p, size_t len)
{
	return crc16(crc, p, len);
}

static const struct crc_variant crc_variant_crc16 = {
	.bits = 16,
	.le = true,
	.poly = 0xa001,
	.func = crc16_wrapper,
};

static void crc16_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc16);
}

static void crc16_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc16_wrapper);
}

/* crc_t10dif */

static u64 crc_t10dif_wrapper(u64 crc, const u8 *p, size_t len)
{
	return crc_t10dif_update(crc, p, len);
}

static const struct crc_variant crc_variant_crc_t10dif = {
	.bits = 16,
	.le = false,
	.poly = 0x8bb7,
	.func = crc_t10dif_wrapper,
};

static void crc_t10dif_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc_t10dif);
}

static void crc_t10dif_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc_t10dif_wrapper);
}

/* crc32_le */

static u64 crc32_le_wrapper(u64 crc, const u8 *p, size_t len)
{
	return crc32_le(crc, p, len);
}

static const struct crc_variant crc_variant_crc32_le = {
	.bits = 32,
	.le = true,
	.poly = 0xedb88320,
	.func = crc32_le_wrapper,
};

static void crc32_le_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc32_le);
}

static void crc32_le_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc32_le_wrapper);
}

/* crc32_be */

static u64 crc32_be_wrapper(u64 crc, const u8 *p, size_t len)
{
	return crc32_be(crc, p, len);
}

static const struct crc_variant crc_variant_crc32_be = {
	.bits = 32,
	.le = false,
	.poly = 0x04c11db7,
	.func = crc32_be_wrapper,
};

static void crc32_be_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc32_be);
}

static void crc32_be_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc32_be_wrapper);
}

/* crc32c */

static u64 crc32c_wrapper(u64 crc, const u8 *p, size_t len)
{
	return crc32c(crc, p, len);
}

static const struct crc_variant crc_variant_crc32c = {
	.bits = 32,
	.le = true,
	.poly = 0x82f63b78,
	.func = crc32c_wrapper,
};

static void crc32c_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc32c);
}

static void crc32c_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc32c_wrapper);
}

/* crc64_be */

static u64 crc64_be_wrapper(u64 crc, const u8 *p, size_t len)
{
	return crc64_be(crc, p, len);
}

static const struct crc_variant crc_variant_crc64_be = {
	.bits = 64,
	.le = false,
	.poly = 0x42f0e1eba9ea3693,
	.func = crc64_be_wrapper,
};

static void crc64_be_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc64_be);
}

static void crc64_be_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc64_be_wrapper);
}

/* crc64_nvme */

static u64 crc64_nvme_wrapper(u64 crc, const u8 *p, size_t len)
{
	/* The inversions that crc64_nvme() does have to be undone here. */
	return ~crc64_nvme(~crc, p, len);
}

static const struct crc_variant crc_variant_crc64_nvme = {
	.bits = 64,
	.le = true,
	.poly = 0x9a6c9329ac4bc9b5,
	.func = crc64_nvme_wrapper,
};

static void crc64_nvme_test(struct kunit *test)
{
	crc_test(test, &crc_variant_crc64_nvme);
}

static void crc64_nvme_benchmark(struct kunit *test)
{
	crc_benchmark(test, crc64_nvme_wrapper);
}

static struct kunit_case crc_test_cases[] = {
	KUNIT_CASE(crc7_be_test),
	KUNIT_CASE(crc7_be_benchmark),
	KUNIT_CASE(crc16_test),
	KUNIT_CASE(crc16_benchmark),
	KUNIT_CASE(crc_t10dif_test),
	KUNIT_CASE(crc_t10dif_benchmark),
	KUNIT_CASE(crc32_le_test),
	KUNIT_CASE(crc32_le_benchmark),
	KUNIT_CASE(crc32_be_test),
	KUNIT_CASE(crc32_be_benchmark),
	KUNIT_CASE(crc32c_test),
	KUNIT_CASE(crc32c_benchmark),
	KUNIT_CASE(crc64_be_test),
	KUNIT_CASE(crc64_be_benchmark),
	KUNIT_CASE(crc64_nvme_test),
	KUNIT_CASE(crc64_nvme_benchmark),
	{},
};

static struct kunit_suite crc_test_suite = {
	.name = "crc",
	.test_cases = crc_test_cases,
	.suite_init = crc_suite_init,
	.suite_exit = crc_suite_exit,
};
kunit_test_suite(crc_test_suite);

MODULE_DESCRIPTION("Unit tests and benchmarks for the CRC library functions");
MODULE_LICENSE("GPL");
