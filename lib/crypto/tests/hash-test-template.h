/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2025 Google LLC
 */
#include <kunit/test.h>
#include <linux/hrtimer.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

/* test_buf is a guarded buffer, i.e. &test_buf[TEST_BUF_LEN] is not mapped. */
#define TEST_BUF_LEN 16384
static u8 *test_buf;

static u8 *orig_test_buf;

static u64 random_seed;

/*
 * This is a simple linear congruential generator.  It is used only for testing,
 * which does not require cryptographically secure random numbers.  A hard-coded
 * algorithm is used instead of <linux/prandom.h> so that it matches the
 * algorithm used by the test vector generation script.  This allows the input
 * data in random test vectors to be concisely stored as just the seed.
 */
static u32 rand32(void)
{
	random_seed = (random_seed * 25214903917 + 11) & ((1ULL << 48) - 1);
	return random_seed >> 16;
}

static void rand_bytes(u8 *out, size_t len)
{
	for (size_t i = 0; i < len; i++)
		out[i] = rand32();
}

static bool rand_bool(void)
{
	return rand32() % 2;
}

/* Generate a random length, preferring small lengths. */
static size_t rand_length(size_t max_len)
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
	return len % (max_len + 1);
}

static size_t rand_offset(size_t max_offset)
{
	return min(rand32() % 128, max_offset);
}

static int hash_suite_init(struct kunit_suite *suite)
{
	/*
	 * Allocate the test buffer using vmalloc() with a page-aligned length
	 * so that it is immediately followed by a guard page.  This allows
	 * buffer overreads to be detected, even in assembly code.
	 */
	size_t alloc_len = round_up(TEST_BUF_LEN, PAGE_SIZE);

	orig_test_buf = vmalloc(alloc_len);
	if (!orig_test_buf)
		return -ENOMEM;

	test_buf = orig_test_buf + alloc_len - TEST_BUF_LEN;
	rand_bytes(test_buf, TEST_BUF_LEN);
	return 0;
}

static void hash_suite_exit(struct kunit_suite *suite)
{
	vfree(orig_test_buf);
	orig_test_buf = NULL;
}

static void rand_bytes_seeded_from_len(u8 *out, size_t len)
{
	random_seed = len;
	rand_bytes(out, len);
}

/*
 * Test that the hash function produces the expected results from the test
 * vectors.
 *
 * Note that it's only necessary to run each test vector in one way (e.g.,
 * one-shot instead of a chain of incremental updates), since consistency
 * between different ways of using the APIs is verified by other test cases.
 */
static void test_hash_test_vectors(struct kunit *test)
{
	for (size_t i = 0; i < ARRAY_SIZE(HASH_TESTVECS); i++) {
		size_t data_len = HASH_TESTVECS[i].data_len;
		u8 actual_digest[HASH_SIZE];

		KUNIT_ASSERT_LE(test, data_len, TEST_BUF_LEN);

		rand_bytes_seeded_from_len(test_buf, data_len);

		HASH(test_buf, data_len, actual_digest);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, actual_digest, HASH_TESTVECS[i].digest, HASH_SIZE,
			"Wrong result with test vector %zu; data_len=%zu", i,
			data_len);
	}
}

/*
 * Test that the hash function produces the same result for a one-shot
 * computation vs. an incremental computation.
 */
static void test_hash_incremental_updates(struct kunit *test)
{
	for (size_t i = 0; i < 1000; i++) {
		size_t total_len, offset;
		struct HASH_CTX ctx;
		u8 hash1[HASH_SIZE];
		u8 hash2[HASH_SIZE];
		size_t num_parts = 0;
		size_t remaining_len, cur_offset;

		total_len = rand_length(TEST_BUF_LEN);
		offset = rand_offset(TEST_BUF_LEN - total_len);

		if (rand32() % 8 == 0)
			/* Refresh the data occasionally. */
			rand_bytes(&test_buf[offset], total_len);

		/* Compute the hash value in one step. */
		HASH(&test_buf[offset], total_len, hash1);

		/* Compute the hash value incrementally. */
		HASH_INIT(&ctx);
		remaining_len = total_len;
		cur_offset = offset;
		while (rand_bool()) {
			size_t part_len = rand_length(remaining_len);

			HASH_UPDATE(&ctx, &test_buf[cur_offset], part_len);
			num_parts++;
			cur_offset += part_len;
			remaining_len -= part_len;
		}
		if (remaining_len != 0 || rand_bool()) {
			HASH_UPDATE(&ctx, &test_buf[cur_offset], remaining_len);
			num_parts++;
		}
		HASH_FINAL(&ctx, hash2);

		/* Compare the two hash values. */
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash1, hash2, HASH_SIZE,
			"Incremental test failed with total_len=%zu num_parts=%zu offset=%zu\n",
			total_len, num_parts, offset);
	}
}

/*
 * Test that the hash function does not overrun any buffers.  Uses a guard page
 * to catch buffer overruns even if they occur in assembly code.
 */
static void test_hash_buffer_overruns(struct kunit *test)
{
	const size_t max_tested_len = TEST_BUF_LEN - sizeof(struct HASH_CTX);
	void *const buf_end = &test_buf[TEST_BUF_LEN];
	struct HASH_CTX *guarded_ctx = buf_end - sizeof(*guarded_ctx);

	for (size_t i = 0; i < 100; i++) {
		size_t len = rand_length(max_tested_len);
		u8 hash[HASH_SIZE];
		struct HASH_CTX ctx;

		/* Check for overruns of the data buffer. */
		HASH(buf_end - len, len, hash);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, buf_end - len, len);
		HASH_FINAL(&ctx, hash);

		/* Check for overruns of the hash value buffer. */
		HASH(test_buf, len, buf_end - HASH_SIZE);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, test_buf, len);
		HASH_FINAL(&ctx, buf_end - HASH_SIZE);

		/* Check for overuns of the hash context. */
		HASH_INIT(guarded_ctx);
		HASH_UPDATE(guarded_ctx, test_buf, len);
		HASH_FINAL(guarded_ctx, hash);
	}
}

/*
 * Test that the caller is permitted to alias the output digest and source data
 * buffer, and also modify the source data buffer after it has been used.
 */
static void test_hash_overlaps(struct kunit *test)
{
	const size_t max_tested_len = TEST_BUF_LEN - HASH_SIZE;
	u8 hash[HASH_SIZE];
	struct HASH_CTX ctx;

	for (size_t i = 0; i < 100; i++) {
		size_t len = rand_length(max_tested_len);
		size_t offset = HASH_SIZE + rand_offset(max_tested_len - len);
		bool left_end = rand_bool();
		u8 *ovl_hash = left_end ? &test_buf[offset] :
					  &test_buf[offset + len - HASH_SIZE];

		HASH(&test_buf[offset], len, hash);
		HASH(&test_buf[offset], len, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 1 failed with len=%zu offset=%zu left_end=%d\n",
			len, offset, left_end);

		HASH(&test_buf[offset], len, hash);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, &test_buf[offset], len);
		HASH_FINAL(&ctx, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 2 failed with len=%zu offset=%zu left_end=%d\n",
			len, offset, left_end);

		HASH(&test_buf[offset], len, hash);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, &test_buf[offset], len);
		rand_bytes(&test_buf[offset], len);
		HASH_FINAL(&ctx, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 3 failed with len=%zu offset=%zu left_end=%d\n",
			len, offset, left_end);
	}
}

/*
 * Test that if the same data is hashed at different alignments in memory, the
 * results are the same.
 */
static void test_hash_alignment_consistency(struct kunit *test)
{
	u8 hash1[128 + HASH_SIZE];
	u8 hash2[128 + HASH_SIZE];

	for (size_t i = 0; i < 100; i++) {
		size_t len = rand_length(TEST_BUF_LEN);
		size_t data_offs1 = rand_offset(TEST_BUF_LEN - len);
		size_t data_offs2 = rand_offset(TEST_BUF_LEN - len);
		size_t hash_offs1 = rand_offset(128);
		size_t hash_offs2 = rand_offset(128);

		rand_bytes(&test_buf[data_offs1], len);
		HASH(&test_buf[data_offs1], len, &hash1[hash_offs1]);
		memmove(&test_buf[data_offs2], &test_buf[data_offs1], len);
		HASH(&test_buf[data_offs2], len, &hash2[hash_offs2]);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, &hash1[hash_offs1], &hash2[hash_offs2], HASH_SIZE,
			"Alignment consistency test failed with len=%zu data_offs=(%zu,%zu) hash_offs=(%zu,%zu)\n",
			len, data_offs1, data_offs2, hash_offs1, hash_offs2);
	}
}

/* Test that HASH_FINAL zeroizes the context. */
static void test_hash_ctx_zeroization(struct kunit *test)
{
	static const u8 zeroes[sizeof(struct HASH_CTX)];
	struct HASH_CTX ctx;

	HASH_INIT(&ctx);
	HASH_UPDATE(&ctx, test_buf, 128);
	HASH_FINAL(&ctx, test_buf);
	KUNIT_EXPECT_MEMEQ_MSG(test, &ctx, zeroes, sizeof(ctx),
			       "Hash context was not zeroized by finalization");
}

#define IRQ_TEST_DATA_LEN 256
#define IRQ_TEST_NUM_BUFFERS 3
#define IRQ_TEST_HRTIMER_INTERVAL us_to_ktime(5)

struct irq_test_state {
	struct hrtimer timer;
	struct work_struct bh_work;
	u8 expected_hashes[IRQ_TEST_NUM_BUFFERS][HASH_SIZE];
	atomic_t seqno;
	atomic_t timer_func_calls;
	atomic_t bh_func_calls;
	bool task_wrong_result;
	bool timer_func_wrong_result;
	bool bh_func_wrong_result;
};

/*
 * Compute a hash of one of the test messages and check for the expected result
 * that was saved earlier in @state->expected_hashes.  To increase the chance of
 * detecting problems, this cycles through multiple messages.
 */
static bool irq_test_step(struct irq_test_state *state)
{
	u32 i = (u32)atomic_inc_return(&state->seqno) % IRQ_TEST_NUM_BUFFERS;
	u8 actual_hash[HASH_SIZE];

	HASH(&test_buf[i * IRQ_TEST_DATA_LEN], IRQ_TEST_DATA_LEN, actual_hash);
	return memcmp(actual_hash, state->expected_hashes[i], HASH_SIZE) == 0;
}

/*
 * This is the timer function run by the IRQ test.  It is called in hardirq
 * context.  It computes a hash, checks for the correct result, then reschedules
 * the timer and also the BH work.
 */
static enum hrtimer_restart irq_test_timer_func(struct hrtimer *timer)
{
	struct irq_test_state *state =
		container_of(timer, typeof(*state), timer);

	WARN_ON_ONCE(!in_hardirq());
	atomic_inc(&state->timer_func_calls);
	if (!irq_test_step(state))
		state->timer_func_wrong_result = true;

	hrtimer_forward_now(&state->timer, IRQ_TEST_HRTIMER_INTERVAL);
	queue_work(system_bh_wq, &state->bh_work);
	return HRTIMER_RESTART;
}

/* Compute a hash in softirq context and check for the expected result. */
static void irq_test_bh_func(struct work_struct *work)
{
	struct irq_test_state *state =
		container_of(work, typeof(*state), bh_work);

	WARN_ON_ONCE(!in_serving_softirq());
	atomic_inc(&state->bh_func_calls);
	if (!irq_test_step(state))
		state->bh_func_wrong_result = true;
}

/*
 * Test that if hashes are computed in parallel in task, softirq, and hardirq
 * context, then all results are as expected.
 *
 * The primary purpose of this test is to verify the correctness of fallback
 * code paths that runs in contexts where the normal code path cannot be used,
 * e.g. !may_use_simd().  These code paths are not covered by any of the other
 * tests, which are executed by the KUnit test runner thread in task context.
 *
 * In addition, this test may detect issues with the architecture's
 * irq_fpu_usable() and kernel_fpu_begin/end() or equivalent functions.
 */
static void test_hash_interrupt_context(struct kunit *test)
{
	struct irq_test_state state = {};
	size_t i;
	unsigned long end_jiffies;

	/* Prepare some test messages and compute the expected hash of each. */
	rand_bytes(test_buf, IRQ_TEST_NUM_BUFFERS * IRQ_TEST_DATA_LEN);
	for (i = 0; i < IRQ_TEST_NUM_BUFFERS; i++)
		HASH(&test_buf[i * IRQ_TEST_DATA_LEN], IRQ_TEST_DATA_LEN,
		     state.expected_hashes[i]);

	/*
	 * Set up a hrtimer (the way we access hardirq context) and a work
	 * struct for the BH workqueue (the way we access softirq context).
	 */
	hrtimer_setup_on_stack(&state.timer, irq_test_timer_func,
			       CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	INIT_WORK(&state.bh_work, irq_test_bh_func);

	/* Run for up to 100000 hashes or 1 second, whichever comes first. */
	end_jiffies = jiffies + HZ;
	hrtimer_start(&state.timer, IRQ_TEST_HRTIMER_INTERVAL,
		      HRTIMER_MODE_REL);
	for (i = 0; i < 100000 && !time_after(jiffies, end_jiffies); i++) {
		if (!irq_test_step(&state))
			state.task_wrong_result = true;
	}

	/* Cancel the timer and work. */
	hrtimer_cancel(&state.timer);
	flush_work(&state.bh_work);

	/* Sanity check: the timer and BH functions should have been run. */
	KUNIT_EXPECT_GT_MSG(test, atomic_read(&state.timer_func_calls), 0,
			    "IRQ test failed; timer function was not called");
	KUNIT_EXPECT_GT_MSG(test, atomic_read(&state.bh_func_calls), 0,
			    "IRQ test failed; BH work function was not called");

	/* Check the results. */
	KUNIT_EXPECT_FALSE_MSG(test, state.task_wrong_result,
			       "IRQ test failed; wrong result in task context");
	KUNIT_EXPECT_FALSE_MSG(
		test, state.timer_func_wrong_result,
		"IRQ test failed; wrong result in timer function (hardirq context)");
	KUNIT_EXPECT_FALSE_MSG(
		test, state.bh_func_wrong_result,
		"IRQ test failed; wrong result in BH work function (softirq context)");
}

#ifdef HMAC
/*
 * Test the corresponding HMAC variant.  This is a bit less thorough than the
 * tests for the hash function, since HMAC is just a small C wrapper around the
 * unkeyed hash function.
 */
static void test_hmac(struct kunit *test)
{
	u8 *raw_key = kunit_kmalloc(test, TEST_BUF_LEN, GFP_KERNEL);
	static const u8 zeroes[sizeof(struct HMAC_CTX)];

	KUNIT_ASSERT_NOT_NULL(test, raw_key);

	for (size_t i = 0; i < ARRAY_SIZE(HMAC_TESTVECS); i++) {
		size_t data_len = HMAC_TESTVECS[i].data_len;
		size_t key_len = HMAC_TESTVECS[i].key_len;
		struct HMAC_CTX ctx;
		struct HMAC_KEY key;
		u8 actual_mac[HASH_SIZE];

		KUNIT_ASSERT_LE(test, data_len, TEST_BUF_LEN);
		KUNIT_ASSERT_LE(test, key_len, TEST_BUF_LEN);

		rand_bytes_seeded_from_len(test_buf, data_len);
		rand_bytes_seeded_from_len(raw_key, key_len);

		HMAC_USINGRAWKEY(raw_key, key_len, test_buf, data_len,
				 actual_mac);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, actual_mac, HMAC_TESTVECS[i].mac, HASH_SIZE,
			"Wrong result with HMAC test vector %zu using raw key; data_len=%zu key_len=%zu",
			i, data_len, key_len);

		memset(actual_mac, 0xff, HASH_SIZE);
		HMAC_SETKEY(&key, raw_key, key_len);
		HMAC(&key, test_buf, data_len, actual_mac);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, actual_mac, HMAC_TESTVECS[i].mac, HASH_SIZE,
			"Wrong result with HMAC test vector %zu using key struct; data_len=%zu key_len=%zu",
			i, data_len, key_len);

		memset(actual_mac, 0xff, HASH_SIZE);
		HMAC_INIT(&ctx, &key);
		HMAC_UPDATE(&ctx, test_buf, data_len);
		HMAC_FINAL(&ctx, actual_mac);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, actual_mac, HMAC_TESTVECS[i].mac, HASH_SIZE,
			"Wrong result with HMAC test vector %zu on init+update+final; data_len=%zu key_len=%zu",
			i, data_len, key_len);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, &ctx, zeroes, sizeof(ctx),
			"HMAC context was not zeroized by finalization");

		memset(actual_mac, 0xff, HASH_SIZE);
		HMAC_INIT(&ctx, &key);
		HMAC_UPDATE(&ctx, test_buf, data_len / 2);
		HMAC_UPDATE(&ctx, &test_buf[data_len / 2], (data_len + 1) / 2);
		HMAC_FINAL(&ctx, actual_mac);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, actual_mac, HMAC_TESTVECS[i].mac, HASH_SIZE,
			"Wrong result with HMAC test vector %zu on init+update+update+final; data_len=%zu key_len=%zu",
			i, data_len, key_len);
	}
}
#endif /* HMAC */

/* Benchmark the hash function on various data lengths. */
static void benchmark_hash(struct kunit *test)
{
	static const size_t lens_to_test[] = {
		1,   16,  64,	127,  128,  200,   256,
		511, 512, 1024, 3173, 4096, 16384,
	};
	size_t len, i, j, num_iters;
	u8 hash[HASH_SIZE];
	u64 t;

	if (!IS_ENABLED(CONFIG_CRYPTO_LIB_BENCHMARK))
		kunit_skip(test, "not enabled");

	/* warm-up */
	for (i = 0; i < 10000000; i += TEST_BUF_LEN)
		HASH(test_buf, TEST_BUF_LEN, hash);

	for (i = 0; i < ARRAY_SIZE(lens_to_test); i++) {
		len = lens_to_test[i];
		KUNIT_ASSERT_LE(test, len, TEST_BUF_LEN);
		num_iters = 10000000 / (len + 128);
		preempt_disable();
		t = ktime_get_ns();
		for (j = 0; j < num_iters; j++)
			HASH(test_buf, len, hash);
		t = ktime_get_ns() - t;
		preempt_enable();
		kunit_info(test, "len=%zu: %llu MB/s\n", len,
			   div64_u64((u64)len * num_iters * 1000, t));
	}
}
