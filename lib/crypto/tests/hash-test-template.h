/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Test cases for hash functions, including a benchmark.  This is included by
 * KUnit test suites that want to use it.  See sha512_kunit.c for an example.
 *
 * Copyright 2025 Google LLC
 */
#include <kunit/run-in-irq-context.h>
#include <kunit/test.h>
#include "test-utils.h"

/*
 * Test the hash function against a list of test vectors.
 *
 * Note that it's only necessary to run each test vector in one way (e.g.,
 * one-shot instead of incremental), since consistency between different ways of
 * using the APIs is verified by other test cases.
 */
static void test_hash_test_vectors(struct kunit *test)
{
	const size_t max_len = 16384;
	u8 *data = alloc_buf(test, max_len);

	for (size_t i = 0; i < ARRAY_SIZE(hash_testvecs); i++) {
		size_t data_len = hash_testvecs[i].data_len;
		u8 actual_hash[HASH_SIZE];

		KUNIT_ASSERT_LE(test, data_len, max_len);
		rand_bytes_seeded_from_len(data, data_len);
		HASH(data, data_len, actual_hash);
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
	const size_t max_len = 4096;
	u8 *data = alloc_buf(test, max_len);
	struct HASH_CTX ctx;
	u8 hash[HASH_SIZE];

	rand_bytes_seeded_from_len(data, max_len);
	HASH_INIT(&ctx);
	for (size_t len = 0; len <= max_len; len++) {
		HASH(data, len, hash);
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
	const size_t max_len = 16384;
	u8 *data = alloc_guarded_buf(test, max_len);

	for (int i = 0; i < 1000; i++) {
		size_t total_len, offset;
		struct HASH_CTX ctx;
		u8 hash1[HASH_SIZE];
		u8 hash2[HASH_SIZE];
		size_t num_parts = 0;
		size_t remaining_len, cur_offset;

		total_len = rand_length(max_len);
		offset = rand_offset(max_len - total_len);
		rand_bytes(&data[offset], total_len);

		/* Compute the hash value in one shot. */
		HASH(&data[offset], total_len, hash1);

		/*
		 * Compute the hash value incrementally, using a randomly
		 * selected sequence of update lengths that sum to total_len.
		 */
		HASH_INIT(&ctx);
		remaining_len = total_len;
		cur_offset = offset;
		while (rand_bool()) {
			size_t part_len = rand_length(remaining_len);

			HASH_UPDATE(&ctx, &data[cur_offset], part_len);
			num_parts++;
			cur_offset += part_len;
			remaining_len -= part_len;
		}
		if (remaining_len != 0 || rand_bool()) {
			HASH_UPDATE(&ctx, &data[cur_offset], remaining_len);
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
	const size_t buf_len = 16384;
	u8 *buf = alloc_guarded_buf(test, buf_len);
	void *const buf_end = &buf[buf_len];
	const size_t max_tested_len = buf_len - sizeof(struct HASH_CTX);
	struct HASH_CTX *guarded_ctx = buf_end - sizeof(*guarded_ctx);

	rand_bytes(buf, buf_len);

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
		HASH(buf, len, buf_end - HASH_SIZE);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, buf, len);
		HASH_FINAL(&ctx, buf_end - HASH_SIZE);

		/* Check for overruns of the hash context. */
		HASH_INIT(guarded_ctx);
		HASH_UPDATE(guarded_ctx, buf, len);
		HASH_FINAL(guarded_ctx, hash);
	}
}

/*
 * Test that the caller is permitted to alias the output digest and source data
 * buffer, and also modify the source data buffer after it has been used.
 */
static void test_hash_overlaps(struct kunit *test)
{
	const size_t buf_len = 16384;
	u8 *buf = alloc_guarded_buf(test, buf_len);
	const size_t max_tested_len = buf_len - HASH_SIZE;
	struct HASH_CTX ctx;
	u8 hash[HASH_SIZE];

	rand_bytes(buf, buf_len);

	for (int i = 0; i < 100; i++) {
		size_t len = rand_length(max_tested_len);
		size_t offset = HASH_SIZE + rand_offset(max_tested_len - len);
		bool left_end = rand_bool();
		u8 *ovl_hash = left_end ? &buf[offset] :
					  &buf[offset + len - HASH_SIZE];

		HASH(&buf[offset], len, hash);
		HASH(&buf[offset], len, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 1 failed with len=%zu offset=%zu left_end=%d",
			len, offset, left_end);

		/* Repeat the above test, but this time use init+update+final */
		HASH(&buf[offset], len, hash);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, &buf[offset], len);
		HASH_FINAL(&ctx, ovl_hash);
		KUNIT_ASSERT_MEMEQ_MSG(
			test, hash, ovl_hash, HASH_SIZE,
			"Overlap test 2 failed with len=%zu offset=%zu left_end=%d",
			len, offset, left_end);

		/* Test modifying the source data after it was used. */
		HASH(&buf[offset], len, hash);
		HASH_INIT(&ctx);
		HASH_UPDATE(&ctx, &buf[offset], len);
		rand_bytes(&buf[offset], len);
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
	const size_t max_len = 16384;
	u8 *data = alloc_guarded_buf(test, max_len);
	u8 hash1[128 + HASH_SIZE];
	u8 hash2[128 + HASH_SIZE];

	for (int i = 0; i < 100; i++) {
		size_t len = rand_length(max_len);
		size_t data_offs1 = rand_offset(max_len - len);
		size_t data_offs2 = rand_offset(max_len - len);
		size_t hash_offs1 = rand_offset(128);
		size_t hash_offs2 = rand_offset(128);

		rand_bytes(&data[data_offs1], len);
		HASH(&data[data_offs1], len, &hash1[hash_offs1]);
		memmove(&data[data_offs2], &data[data_offs1], len);
		HASH(&data[data_offs2], len, &hash2[hash_offs2]);
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
	const size_t data_len = 128;
	u8 *data = alloc_buf(test, data_len);
	u8 hash[HASH_SIZE];

	rand_bytes(data, data_len);
	HASH_INIT(&ctx);
	HASH_UPDATE(&ctx, data, data_len);
	HASH_FINAL(&ctx, hash);
	KUNIT_ASSERT_MEMEQ_MSG(test, &ctx, zeroes, sizeof(ctx),
			       "Hash context was not zeroized by finalization");
}

#define IRQ_TEST_DATA_LEN 256
#define IRQ_TEST_NUM_BUFFERS 3 /* matches max concurrency level */

struct hash_irq_test1_state {
	u8 *data;
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

	HASH(&state->data[i * IRQ_TEST_DATA_LEN], IRQ_TEST_DATA_LEN,
	     actual_hash);
	return memcmp(actual_hash, state->expected_hashes[i], HASH_SIZE) == 0;
}

/*
 * Test that if hashes are computed in task, softirq, and hardirq context
 * concurrently, then all results are as expected.
 */
static void test_hash_interrupt_context_1(struct kunit *test)
{
	const size_t total_data_len = IRQ_TEST_NUM_BUFFERS * IRQ_TEST_DATA_LEN;
	struct hash_irq_test1_state state = {};

	/* Prepare some test messages and compute the expected hash of each. */
	state.data = alloc_buf(test, total_data_len);
	rand_bytes(state.data, total_data_len);
	for (int i = 0; i < IRQ_TEST_NUM_BUFFERS; i++)
		HASH(&state.data[i * IRQ_TEST_DATA_LEN], IRQ_TEST_DATA_LEN,
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
	u8 *data;
	size_t data_len;
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
		HASH_UPDATE(&ctx->hash_ctx, &state->data[ctx->offset],
			    state->update_lens[ctx->step - 1]);
		ctx->offset += state->update_lens[ctx->step - 1];
		ctx->step++;
	} else {
		/* Final step */
		u8 actual_hash[HASH_SIZE];

		if (WARN_ON_ONCE(ctx->offset != state->data_len))
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
	const size_t data_len = 16384;
	struct hash_irq_test2_state *state;
	size_t remaining = data_len;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	state->data_len = data_len;
	state->data = alloc_buf(test, data_len);

	rand_bytes(state->data, data_len);
	HASH(state->data, data_len, state->expected_hash);

	/*
	 * Generate a list of update lengths to use.  Ensure that it contains
	 * multiple entries but is limited to a maximum length.
	 */
	KUNIT_ASSERT_GT(test, data_len / 4096, 1);
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
	const size_t max_data_len = 4096;
	const size_t max_key_len = 293;
	const size_t outer_key_len = 32;
	u8 *data = alloc_guarded_buf(test, max_data_len);
	u8 *raw_key = alloc_guarded_buf(test, max_key_len);
	static const u8 zeroes[sizeof(struct HMAC_CTX)];
	struct HMAC_KEY key;
	struct HMAC_CTX ctx;
	u8 mac[HASH_SIZE];
	u8 mac2[HASH_SIZE];

	rand_bytes_seeded_from_len(data, max_data_len);
	rand_bytes_seeded_from_len(raw_key, outer_key_len);

	HMAC_PREPAREKEY(&key, raw_key, outer_key_len);
	HMAC_INIT(&ctx, &key);
	for (size_t data_len = 0; data_len <= max_data_len; data_len++) {
		/*
		 * Cycle through key lengths as well.  Somewhat arbitrarily go
		 * up to 293, which is somewhat larger than the largest hash
		 * block size (which is the size at which the key starts being
		 * hashed down to one block); going higher would not be useful.
		 * To reduce correlation with data_len, use a prime number here.
		 */
		size_t key_len = data_len % max_key_len;

		HMAC_UPDATE(&ctx, data, data_len);

		rand_bytes_seeded_from_len(raw_key, key_len);
		HMAC_USINGRAWKEY(raw_key, key_len, data, data_len, mac);
		HMAC_UPDATE(&ctx, mac, HASH_SIZE);

		/* Verify that HMAC() is consistent with HMAC_USINGRAWKEY(). */
		HMAC_PREPAREKEY(&key, raw_key, key_len);
		HMAC(&key, data, data_len, mac2);
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
	const size_t max_len = 16384;
	u8 *data = alloc_buf(test, max_len);
	u8 hash[HASH_SIZE];

	if (!IS_ENABLED(CONFIG_CRYPTO_LIB_BENCHMARK))
		kunit_skip(test, "not enabled");

	/* Warm-up */
	memset(data, 0, max_len);
	for (size_t i = 0; i < 10000000; i += max_len)
		HASH(data, max_len, hash);

	for (size_t i = 0; i < ARRAY_SIZE(lens_to_test); i++) {
		size_t len = lens_to_test[i];
		/* The '+ 128' tries to account for per-message overhead. */
		size_t num_iters = 10000000 / (len + 128);
		u64 t;

		KUNIT_ASSERT_LE(test, len, max_len);
		preempt_disable();
		t = ktime_get_ns();
		for (size_t j = 0; j < num_iters; j++)
			HASH(data, len, hash);
		t = ktime_get_ns() - t;
		preempt_enable();
		kunit_info(test, "len=%zu: %llu MB/s", len,
			   div64_u64((u64)len * num_iters * 1000, t ?: 1));
	}
}
