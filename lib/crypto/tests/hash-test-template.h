/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Test cases for hash functions, including a benchmark.  This is included by
 * KUnit test suites that want to use it.  See sha512_kunit.c for an example.
 *
 * Copyright 2025 Google LLC
 */
#include <kunit/run-in-irq-context.h>
#include <kunit/test.h>
#include <linux/vmalloc.h>

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

static void rand_bytes_seeded_from_len(u8 *out, size_t len)
{
	random_seed = len;
	rand_bytes(out, len);
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
	return 0;
}

static void hash_suite_exit(struct kunit_suite *suite)
{
	vfree(orig_test_buf);
	orig_test_buf = NULL;
	test_buf = NULL;
}

/*
 * Test the hash function against a list of test vectors.
 *
 * Note that it's only necessary to run each test vector in one way (e.g.,
 * one-shot instead of incremental), since consistency between different ways of
 * using the APIs is verified by other test cases.
 */
static void test_hash_test_vectors(struct kunit *test)
{
	for (size_t i = 0; i < ARRAY_SIZE(hash_testvecs); i++) {
		size_t data_len = hash_testvecs[i].data_len;
		u8 actual_hash[HASH_SIZE];

		KUNIT_ASSERT_LE(test, data_len, TEST_BUF_LEN);
		rand_bytes_seeded_from_len(test_buf, data_len);

		HASH(test_buf, data_len, actual_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, actual_hash, hash_testvecs[i].digest, HASH_SIZE,
			"Wrong result with test vector %zu; data_len=%zu", i,
			data_len);
	}
}

/*
 * Test that the hash function produces correct results for *every* length up to
 * 4096 bytes.  To do this, generate seeded random data, then calculate a hash
 * value for each length 0..4096, then hash the hash values.  Verify just the
 * final hash value, which should match only when all hash values were correct.
 */
static void test_hash_all_lens_up_to_4096(struct kunit *test)
{
	struct HASH_CTX ctx;
	u8 hash[HASH_SIZE];

	static_assert(TEST_BUF_LEN >= 4096);
	rand_bytes_seeded_from_len(test_buf, 4096);
	HASH_INIT(&ctx);
	for (size_t len = 0; len <= 4096; len++) {
		HASH(test_buf, len, hash);
		HASH_UPDATE(&ctx, hash, HASH_SIZE);
	}
	HASH_FINAL(&ctx, hash);
	KUNIT_ASSERT_MEMEQ(test, hash, hash_testvec_consolidated, HASH_SIZE);
}

/*
 * Test that the hash function produces the same result with a one-shot
 * computation as it does with an incremental computation.
 */
static void test_hash_incremental_updates(struct kunit *test)
{
	for (int i = 0; i < 1000; i++) {
		size_t total_len, offset;
		struct HASH_CTX ctx;
		u8 hash1[HASH_SIZE];
		u8 hash2[HASH_SIZE];
		size_t num_parts = 0;
		size_t remaining_len, cur_offset;

		total_len = rand_length(TEST_BUF_LEN);
		offset = rand_offset(TEST_BUF_LEN - total_len);
		rand_bytes(&test_buf[offset], total_len);

		/* Compute the hash value in one shot. */
		HASH(&test_buf[offset], total_len, hash1);

		/*
		 * Compute the hash value incrementally, using a randomly
		 * selected sequence of update lengths that sum to total_len.
		 */
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

		/* Verify that the two hash values are the same. */
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash1, hash2, HASH_SIZE,
			"Incremental test failed with total_len=%zu num_parts=%zu offset=%zu",
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

	rand_bytes(test_buf, TEST_BUF_LEN);

	for (int i = 0; i < 100; i++) {
		size_t len = rand_length(max_tested_len);
		struct HASH_CTX ctx;
		u8 hash[HASH_SIZE];

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
	struct HASH_CTX ctx;
	u8 hash[HASH_SIZE];

	rand_bytes(test_buf, TEST_BUF_LEN);

	for (int i = 0; i < 100; i++) {
		size_t len = rand_length(max_tested_len);
		size_t offset = HASH_SIZE + rand_offset(max_tested_len - len);
		bool left_end = rand_bool();
		u8 *ovl_hash = left_end ? &test_buf[offset] :
					  &test_buf[offset + len - HASH_SIZE];

		HASH(&test_buf[offset], len, hash);
		HASH(&test_buf[offset], len, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 1 failed with len=%zu offset=%zu left_end=%d",
			len, offset, left_end);

		/* Repeat the above test, but this time use init+update+final */
		HASH(&test_buf[offset], len, hash);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, &test_buf[offset], len);
		HASH_FINAL(&ctx, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 2 failed with len=%zu offset=%zu left_end=%d",
			len, offset, left_end);

		/* Test modifying the source data after it was used. */
		HASH(&test_buf[offset], len, hash);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, &test_buf[offset], len);
		rand_bytes(&test_buf[offset], len);
		HASH_FINAL(&ctx, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 3 failed with len=%zu offset=%zu left_end=%d",
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

	for (int i = 0; i < 100; i++) {
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
			"Alignment consistency test failed with len=%zu data_offs=(%zu,%zu) hash_offs=(%zu,%zu)",
			len, data_offs1, data_offs2, hash_offs1, hash_offs2);
	}
}

/* Test that HASH_FINAL zeroizes the context. */
static void test_hash_ctx_zeroization(struct kunit *test)
{
	static const u8 zeroes[sizeof(struct HASH_CTX)];
	struct HASH_CTX ctx;

	rand_bytes(test_buf, 128);
	HASH_INIT(&ctx);
	HASH_UPDATE(&ctx, test_buf, 128);
	HASH_FINAL(&ctx, test_buf);
	KUNIT_ASSERT_MEMEQ_MSG(test, &ctx, zeroes, sizeof(ctx),
			       "Hash context was not zeroized by finalization");
}

#define IRQ_TEST_DATA_LEN 256
#define IRQ_TEST_NUM_BUFFERS 3 /* matches max concurrency level */

struct hash_irq_test1_state {
	u8 expected_hashes[IRQ_TEST_NUM_BUFFERS][HASH_SIZE];
	atomic_t seqno;
};

/*
 * Compute the hash of one of the test messages and verify that it matches the
 * expected hash from @state->expected_hashes.  To increase the chance of
 * detecting problems, cycle through multiple messages.
 */
static bool hash_irq_test1_func(void *state_)
{
	struct hash_irq_test1_state *state = state_;
	u32 i = (u32)atomic_inc_return(&state->seqno) % IRQ_TEST_NUM_BUFFERS;
	u8 actual_hash[HASH_SIZE];

	HASH(&test_buf[i * IRQ_TEST_DATA_LEN], IRQ_TEST_DATA_LEN, actual_hash);
	return memcmp(actual_hash, state->expected_hashes[i], HASH_SIZE) == 0;
}

/*
 * Test that if hashes are computed in task, softirq, and hardirq context
 * concurrently, then all results are as expected.
 */
static void test_hash_interrupt_context_1(struct kunit *test)
{
	struct hash_irq_test1_state state = {};

	/* Prepare some test messages and compute the expected hash of each. */
	rand_bytes(test_buf, IRQ_TEST_NUM_BUFFERS * IRQ_TEST_DATA_LEN);
	for (int i = 0; i < IRQ_TEST_NUM_BUFFERS; i++)
		HASH(&test_buf[i * IRQ_TEST_DATA_LEN], IRQ_TEST_DATA_LEN,
		     state.expected_hashes[i]);

	kunit_run_irq_test(test, hash_irq_test1_func, 100000, &state);
}

struct hash_irq_test2_hash_ctx {
	struct HASH_CTX hash_ctx;
	atomic_t in_use;
	int offset;
	int step;
};

struct hash_irq_test2_state {
	struct hash_irq_test2_hash_ctx ctxs[IRQ_TEST_NUM_BUFFERS];
	u8 expected_hash[HASH_SIZE];
	u16 update_lens[32];
	int num_steps;
};

static bool hash_irq_test2_func(void *state_)
{
	struct hash_irq_test2_state *state = state_;
	struct hash_irq_test2_hash_ctx *ctx;
	bool ret = true;

	for (ctx = &state->ctxs[0]; ctx < &state->ctxs[ARRAY_SIZE(state->ctxs)];
	     ctx++) {
		if (atomic_cmpxchg(&ctx->in_use, 0, 1) == 0)
			break;
	}
	if (WARN_ON_ONCE(ctx == &state->ctxs[ARRAY_SIZE(state->ctxs)])) {
		/*
		 * This should never happen, as the number of contexts is equal
		 * to the maximum concurrency level of kunit_run_irq_test().
		 */
		return false;
	}

	if (ctx->step == 0) {
		/* Init step */
		HASH_INIT(&ctx->hash_ctx);
		ctx->offset = 0;
		ctx->step++;
	} else if (ctx->step < state->num_steps - 1) {
		/* Update step */
		HASH_UPDATE(&ctx->hash_ctx, &test_buf[ctx->offset],
			    state->update_lens[ctx->step - 1]);
		ctx->offset += state->update_lens[ctx->step - 1];
		ctx->step++;
	} else {
		/* Final step */
		u8 actual_hash[HASH_SIZE];

		if (WARN_ON_ONCE(ctx->offset != TEST_BUF_LEN))
			ret = false;
		HASH_FINAL(&ctx->hash_ctx, actual_hash);
		if (memcmp(actual_hash, state->expected_hash, HASH_SIZE) != 0)
			ret = false;
		ctx->step = 0;
	}
	atomic_set_release(&ctx->in_use, 0);
	return ret;
}

/*
 * Test that if hashes are computed in task, softirq, and hardirq context
 * concurrently, *including doing different parts of the same incremental
 * computation in different contexts*, then all results are as expected.
 * Besides detecting bugs similar to those that test_hash_interrupt_context_1
 * can detect, this test case can also detect bugs where hash function
 * implementations don't correctly handle these mixed incremental computations.
 */
static void test_hash_interrupt_context_2(struct kunit *test)
{
	struct hash_irq_test2_state *state;
	int remaining = TEST_BUF_LEN;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	rand_bytes(test_buf, TEST_BUF_LEN);
	HASH(test_buf, TEST_BUF_LEN, state->expected_hash);

	/*
	 * Generate a list of update lengths to use.  Ensure that it contains
	 * multiple entries but is limited to a maximum length.
	 */
	static_assert(TEST_BUF_LEN / 4096 > 1);
	for (state->num_steps = 0;
	     state->num_steps < ARRAY_SIZE(state->update_lens) - 1 && remaining;
	     state->num_steps++) {
		state->update_lens[state->num_steps] =
			rand_length(min(remaining, 4096));
		remaining -= state->update_lens[state->num_steps];
	}
	if (remaining)
		state->update_lens[state->num_steps++] = remaining;
	state->num_steps += 2; /* for init and final */

	kunit_run_irq_test(test, hash_irq_test2_func, 250000, state);
}

#define UNKEYED_HASH_KUNIT_CASES                     \
	KUNIT_CASE(test_hash_test_vectors),          \
	KUNIT_CASE(test_hash_all_lens_up_to_4096),   \
	KUNIT_CASE(test_hash_incremental_updates),   \
	KUNIT_CASE(test_hash_buffer_overruns),       \
	KUNIT_CASE(test_hash_overlaps),              \
	KUNIT_CASE(test_hash_alignment_consistency), \
	KUNIT_CASE(test_hash_ctx_zeroization),       \
	KUNIT_CASE(test_hash_interrupt_context_1),   \
	KUNIT_CASE(test_hash_interrupt_context_2)
/* benchmark_hash is omitted so that the suites can put it last. */

#ifdef HMAC
/*
 * Test the corresponding HMAC variant.
 *
 * This test case is fairly short, since HMAC is just a simple C wrapper around
 * the underlying unkeyed hash function, which is already well-tested by the
 * other test cases.  It's not useful to test things like data alignment or
 * interrupt context again for HMAC, nor to have a long list of test vectors.
 *
 * Thus, just do a single consolidated test, which covers all data lengths up to
 * 4096 bytes and all key lengths up to 292 bytes.  For each data length, select
 * a key length, generate the inputs from a seed, and compute the HMAC value.
 * Concatenate all these HMAC values together, and compute the HMAC of that.
 * Verify that value.  If this fails, then the HMAC implementation is wrong.
 * This won't show which specific input failed, but that should be fine.  Any
 * failure would likely be non-input-specific or also show in the unkeyed tests.
 */
static void test_hmac(struct kunit *test)
{
	static const u8 zeroes[sizeof(struct HMAC_CTX)];
	u8 *raw_key;
	struct HMAC_KEY key;
	struct HMAC_CTX ctx;
	u8 mac[HASH_SIZE];
	u8 mac2[HASH_SIZE];

	static_assert(TEST_BUF_LEN >= 4096 + 293);
	rand_bytes_seeded_from_len(test_buf, 4096);
	raw_key = &test_buf[4096];

	rand_bytes_seeded_from_len(raw_key, 32);
	HMAC_PREPAREKEY(&key, raw_key, 32);
	HMAC_INIT(&ctx, &key);
	for (size_t data_len = 0; data_len <= 4096; data_len++) {
		/*
		 * Cycle through key lengths as well.  Somewhat arbitrarily go
		 * up to 293, which is somewhat larger than the largest hash
		 * block size (which is the size at which the key starts being
		 * hashed down to one block); going higher would not be useful.
		 * To reduce correlation with data_len, use a prime number here.
		 */
		size_t key_len = data_len % 293;

		HMAC_UPDATE(&ctx, test_buf, data_len);

		rand_bytes_seeded_from_len(raw_key, key_len);
		HMAC_USINGRAWKEY(raw_key, key_len, test_buf, data_len, mac);
		HMAC_UPDATE(&ctx, mac, HASH_SIZE);

		/* Verify that HMAC() is consistent with HMAC_USINGRAWKEY(). */
		HMAC_PREPAREKEY(&key, raw_key, key_len);
		HMAC(&key, test_buf, data_len, mac2);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, mac, mac2, HASH_SIZE,
			"HMAC gave different results with raw and prepared keys");
	}
	HMAC_FINAL(&ctx, mac);
	KUNIT_EXPECT_MEMEQ_MSG(test, mac, hmac_testvec_consolidated, HASH_SIZE,
			       "HMAC gave wrong result");
	KUNIT_EXPECT_MEMEQ_MSG(test, &ctx, zeroes, sizeof(ctx),
			       "HMAC context was not zeroized by finalization");
}
#define HASH_KUNIT_CASES UNKEYED_HASH_KUNIT_CASES, KUNIT_CASE(test_hmac)
#else
#define HASH_KUNIT_CASES UNKEYED_HASH_KUNIT_CASES
#endif

/* Benchmark the hash function on various data lengths. */
static void benchmark_hash(struct kunit *test)
{
	static const size_t lens_to_test[] = {
		1,   16,  64,	127,  128,  200,   256,
		511, 512, 1024, 3173, 4096, 16384,
	};
	u8 hash[HASH_SIZE];

	if (!IS_ENABLED(CONFIG_CRYPTO_LIB_BENCHMARK))
		kunit_skip(test, "not enabled");

	/* Warm-up */
	for (size_t i = 0; i < 10000000; i += TEST_BUF_LEN)
		HASH(test_buf, TEST_BUF_LEN, hash);

	for (size_t i = 0; i < ARRAY_SIZE(lens_to_test); i++) {
		size_t len = lens_to_test[i];
		/* The '+ 128' tries to account for per-message overhead. */
		size_t num_iters = 10000000 / (len + 128);
		u64 t;

		KUNIT_ASSERT_LE(test, len, TEST_BUF_LEN);
		preempt_disable();
		t = ktime_get_ns();
		for (size_t j = 0; j < num_iters; j++)
			HASH(test_buf, len, hash);
		t = ktime_get_ns() - t;
		preempt_enable();
		kunit_info(test, "len=%zu: %llu MB/s", len,
			   div64_u64((u64)len * num_iters * 1000, t ?: 1));
	}
}
