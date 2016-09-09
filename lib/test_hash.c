/*
 * Test cases for <linux/hash.h> and <linux/stringhash.h>
 * This just verifies that various ways of computing a hash
 * produce the same thing and, for cases where a k-bit hash
 * value is requested, is of the requested size.
 *
 * We fill a buffer with a 255-byte null-terminated string,
 * and use both full_name_hash() and hashlen_string() to hash the
 * substrings from i to j, where 0 <= i < j < 256.
 *
 * The returned values are used to check that __hash_32() and
 * __hash_32_generic() compute the same thing.  Likewise hash_32()
 * and hash_64().
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt "\n"

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/hash.h>
#include <linux/stringhash.h>
#include <linux/printk.h>

/* 32-bit XORSHIFT generator.  Seed must not be zero. */
static u32 __init __attribute_const__
xorshift(u32 seed)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

/* Given a non-zero x, returns a non-zero byte. */
static u8 __init __attribute_const__
mod255(u32 x)
{
	x = (x & 0xffff) + (x >> 16);	/* 1 <= x <= 0x1fffe */
	x = (x & 0xff) + (x >> 8);	/* 1 <= x <= 0x2fd */
	x = (x & 0xff) + (x >> 8);	/* 1 <= x <= 0x100 */
	x = (x & 0xff) + (x >> 8);	/* 1 <= x <= 0xff */
	return x;
}

/* Fill the buffer with non-zero bytes. */
static void __init
fill_buf(char *buf, size_t len, u32 seed)
{
	size_t i;

	for (i = 0; i < len; i++) {
		seed = xorshift(seed);
		buf[i] = mod255(seed);
	}
}

/*
 * Test the various integer hash functions.  h64 (or its low-order bits)
 * is the integer to hash.  hash_or accumulates the OR of the hash values,
 * which are later checked to see that they cover all the requested bits.
 *
 * Because these functions (as opposed to the string hashes) are all
 * inline, the code being tested is actually in the module, and you can
 * recompile and re-test the module without rebooting.
 */
static bool __init
test_int_hash(unsigned long long h64, u32 hash_or[2][33])
{
	int k;
	u32 h0 = (u32)h64, h1, h2;

	/* Test __hash32 */
	hash_or[0][0] |= h1 = __hash_32(h0);
#ifdef HAVE_ARCH__HASH_32
	hash_or[1][0] |= h2 = __hash_32_generic(h0);
#if HAVE_ARCH__HASH_32 == 1
	if (h1 != h2) {
		pr_err("__hash_32(%#x) = %#x != __hash_32_generic() = %#x",
			h0, h1, h2);
		return false;
	}
#endif
#endif

	/* Test k = 1..32 bits */
	for (k = 1; k <= 32; k++) {
		u32 const m = ((u32)2 << (k-1)) - 1;	/* Low k bits set */

		/* Test hash_32 */
		hash_or[0][k] |= h1 = hash_32(h0, k);
		if (h1 > m) {
			pr_err("hash_32(%#x, %d) = %#x > %#x", h0, k, h1, m);
			return false;
		}
#ifdef HAVE_ARCH_HASH_32
		h2 = hash_32_generic(h0, k);
#if HAVE_ARCH_HASH_32 == 1
		if (h1 != h2) {
			pr_err("hash_32(%#x, %d) = %#x != hash_32_generic() "
				" = %#x", h0, k, h1, h2);
			return false;
		}
#else
		if (h2 > m) {
			pr_err("hash_32_generic(%#x, %d) = %#x > %#x",
				h0, k, h1, m);
			return false;
		}
#endif
#endif
		/* Test hash_64 */
		hash_or[1][k] |= h1 = hash_64(h64, k);
		if (h1 > m) {
			pr_err("hash_64(%#llx, %d) = %#x > %#x", h64, k, h1, m);
			return false;
		}
#ifdef HAVE_ARCH_HASH_64
		h2 = hash_64_generic(h64, k);
#if HAVE_ARCH_HASH_64 == 1
		if (h1 != h2) {
			pr_err("hash_64(%#llx, %d) = %#x != hash_64_generic() "
				"= %#x", h64, k, h1, h2);
			return false;
		}
#else
		if (h2 > m) {
			pr_err("hash_64_generic(%#llx, %d) = %#x > %#x",
				h64, k, h1, m);
			return false;
		}
#endif
#endif
	}

	(void)h2;	/* Suppress unused variable warning */
	return true;
}

#define SIZE 256	/* Run time is cubic in SIZE */

static int __init
test_hash_init(void)
{
	char buf[SIZE+1];
	u32 string_or = 0, hash_or[2][33] = { 0 };
	unsigned tests = 0;
	unsigned long long h64 = 0;
	int i, j;

	fill_buf(buf, SIZE, 1);

	/* Test every possible non-empty substring in the buffer. */
	for (j = SIZE; j > 0; --j) {
		buf[j] = '\0';

		for (i = 0; i <= j; i++) {
			u64 hashlen = hashlen_string(buf+i);
			u32 h0 = full_name_hash(buf+i, j-i);

			/* Check that hashlen_string gets the length right */
			if (hashlen_len(hashlen) != j-i) {
				pr_err("hashlen_string(%d..%d) returned length"
					" %u, expected %d",
					i, j, hashlen_len(hashlen), j-i);
				return -EINVAL;
			}
			/* Check that the hashes match */
			if (hashlen_hash(hashlen) != h0) {
				pr_err("hashlen_string(%d..%d) = %08x != "
					"full_name_hash() = %08x",
					i, j, hashlen_hash(hashlen), h0);
				return -EINVAL;
			}

			string_or |= h0;
			h64 = h64 << 32 | h0;	/* For use with hash_64 */
			if (!test_int_hash(h64, hash_or))
				return -EINVAL;
			tests++;
		} /* i */
	} /* j */

	/* The OR of all the hash values should cover all the bits */
	if (~string_or) {
		pr_err("OR of all string hash results = %#x != %#x",
			string_or, -1u);
		return -EINVAL;
	}
	if (~hash_or[0][0]) {
		pr_err("OR of all __hash_32 results = %#x != %#x",
			hash_or[0][0], -1u);
		return -EINVAL;
	}
#ifdef HAVE_ARCH__HASH_32
#if HAVE_ARCH__HASH_32 != 1	/* Test is pointless if results match */
	if (~hash_or[1][0]) {
		pr_err("OR of all __hash_32_generic results = %#x != %#x",
			hash_or[1][0], -1u);
		return -EINVAL;
	}
#endif
#endif

	/* Likewise for all the i-bit hash values */
	for (i = 1; i <= 32; i++) {
		u32 const m = ((u32)2 << (i-1)) - 1;	/* Low i bits set */

		if (hash_or[0][i] != m) {
			pr_err("OR of all hash_32(%d) results = %#x "
				"(%#x expected)", i, hash_or[0][i], m);
			return -EINVAL;
		}
		if (hash_or[1][i] != m) {
			pr_err("OR of all hash_64(%d) results = %#x "
				"(%#x expected)", i, hash_or[1][i], m);
			return -EINVAL;
		}
	}

	/* Issue notices about skipped tests. */
#ifndef HAVE_ARCH__HASH_32
	pr_info("__hash_32() has no arch implementation to test.");
#elif HAVE_ARCH__HASH_32 != 1
	pr_info("__hash_32() is arch-specific; not compared to generic.");
#endif
#ifndef HAVE_ARCH_HASH_32
	pr_info("hash_32() has no arch implementation to test.");
#elif HAVE_ARCH_HASH_32 != 1
	pr_info("hash_32() is arch-specific; not compared to generic.");
#endif
#ifndef HAVE_ARCH_HASH_64
	pr_info("hash_64() has no arch implementation to test.");
#elif HAVE_ARCH_HASH_64 != 1
	pr_info("hash_64() is arch-specific; not compared to generic.");
#endif

	pr_notice("%u tests passed.", tests);

	return 0;
}

static void __exit test_hash_exit(void)
{
}

module_init(test_hash_init);	/* Does everything */
module_exit(test_hash_exit);	/* Does nothing */

MODULE_LICENSE("GPL");
