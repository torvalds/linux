/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Test utility functions shared by the crypto library tests.
 *
 * For now this is simply a header that's included into the KUnit test suites
 * that need it.  If this gets too large it could be made its own translation
 * unit and libcrypto_test_utils module, but that seems overkill for now.
 */
#ifndef LIB_CRYPTO_TEST_UTILS_H
#define LIB_CRYPTO_TEST_UTILS_H

#include <kunit/test.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

static u64 random_seed;

static __maybe_unused void action_free_guarded_buf(void *buf)
{
	vfree(buf);
}

/*
 * Allocate a KUnit-managed buffer that has length @size bytes (> 0) immediately
 * followed by an unmapped page, and assert that the allocation succeeds.
 */
static __maybe_unused void *alloc_guarded_buf(struct kunit *test, size_t size)
{
	size_t full_size = round_up(size, PAGE_SIZE);
	void *buf = vmalloc(full_size);

	KUNIT_ASSERT_NOT_NULL(test, buf);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, action_free_guarded_buf,
						  buf));
	return buf + full_size - size;
}

static __maybe_unused void *alloc_buf(struct kunit *test, size_t size)
{
	void *buf = kunit_kmalloc(test, size, GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, buf);
	return buf;
}

static __maybe_unused void *memdup_buf(struct kunit *test, const void *src,
				       size_t size)
{
	void *dst = alloc_buf(test, size);

	return memcpy(dst, src, size);
}

/*
 * This is a simple linear congruential generator.  It is used only for testing,
 * which does not require cryptographically secure random numbers.  A hard-coded
 * algorithm is used instead of <linux/prandom.h> so that it matches the
 * algorithm used by the test vector generation script.  This allows the input
 * data in random test vectors to be concisely stored as just the seed.
 */
static __maybe_unused u32 rand32(void)
{
	random_seed = (random_seed * 25214903917 + 11) & ((1ULL << 48) - 1);
	return random_seed >> 16;
}

static __maybe_unused void rand_bytes(u8 *out, size_t len)
{
	for (size_t i = 0; i < len; i++)
		out[i] = rand32();
}

static __maybe_unused void rand_bytes_seeded_from_len(u8 *out, size_t len)
{
	random_seed = len;
	rand_bytes(out, len);
}

static __maybe_unused bool rand_bool(void)
{
	return rand32() % 2;
}

/* Generate a random length, preferring small lengths. */
static __maybe_unused size_t rand_length(size_t max_len)
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

static __maybe_unused size_t rand_offset(size_t max_offset)
{
	return min(rand32() % 128, max_offset);
}

#endif /* LIB_CRYPTO_TEST_UTILS_H */
