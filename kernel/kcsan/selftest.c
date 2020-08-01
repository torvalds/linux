// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/types.h>

#include "encoding.h"

#define ITERS_PER_TEST 2000

/* Test requirements. */
static bool test_requires(void)
{
	/* random should be initialized for the below tests */
	return prandom_u32() + prandom_u32() != 0;
}

/*
 * Test watchpoint encode and decode: check that encoding some access's info,
 * and then subsequent decode preserves the access's info.
 */
static bool test_encode_decode(void)
{
	int i;

	for (i = 0; i < ITERS_PER_TEST; ++i) {
		size_t size = prandom_u32_max(MAX_ENCODABLE_SIZE) + 1;
		bool is_write = !!prandom_u32_max(2);
		unsigned long addr;

		prandom_bytes(&addr, sizeof(addr));
		if (WARN_ON(!check_encodable(addr, size)))
			return false;

		/* Encode and decode */
		{
			const long encoded_watchpoint =
				encode_watchpoint(addr, size, is_write);
			unsigned long verif_masked_addr;
			size_t verif_size;
			bool verif_is_write;

			/* Check special watchpoints */
			if (WARN_ON(decode_watchpoint(
				    INVALID_WATCHPOINT, &verif_masked_addr,
				    &verif_size, &verif_is_write)))
				return false;
			if (WARN_ON(decode_watchpoint(
				    CONSUMED_WATCHPOINT, &verif_masked_addr,
				    &verif_size, &verif_is_write)))
				return false;

			/* Check decoding watchpoint returns same data */
			if (WARN_ON(!decode_watchpoint(
				    encoded_watchpoint, &verif_masked_addr,
				    &verif_size, &verif_is_write)))
				return false;
			if (WARN_ON(verif_masked_addr !=
				    (addr & WATCHPOINT_ADDR_MASK)))
				goto fail;
			if (WARN_ON(verif_size != size))
				goto fail;
			if (WARN_ON(is_write != verif_is_write))
				goto fail;

			continue;
fail:
			pr_err("%s fail: %s %zu bytes @ %lx -> encoded: %lx -> %s %zu bytes @ %lx\n",
			       __func__, is_write ? "write" : "read", size,
			       addr, encoded_watchpoint,
			       verif_is_write ? "write" : "read", verif_size,
			       verif_masked_addr);
			return false;
		}
	}

	return true;
}

/* Test access matching function. */
static bool test_matching_access(void)
{
	if (WARN_ON(!matching_access(10, 1, 10, 1)))
		return false;
	if (WARN_ON(!matching_access(10, 2, 11, 1)))
		return false;
	if (WARN_ON(!matching_access(10, 1, 9, 2)))
		return false;
	if (WARN_ON(matching_access(10, 1, 11, 1)))
		return false;
	if (WARN_ON(matching_access(9, 1, 10, 1)))
		return false;

	/*
	 * An access of size 0 could match another access, as demonstrated here.
	 * Rather than add more comparisons to 'matching_access()', which would
	 * end up in the fast-path for *all* checks, check_access() simply
	 * returns for all accesses of size 0.
	 */
	if (WARN_ON(!matching_access(8, 8, 12, 0)))
		return false;

	return true;
}

static int __init kcsan_selftest(void)
{
	int passed = 0;
	int total = 0;

#define RUN_TEST(do_test)                                                      \
	do {                                                                   \
		++total;                                                       \
		if (do_test())                                                 \
			++passed;                                              \
		else                                                           \
			pr_err("KCSAN selftest: " #do_test " failed");         \
	} while (0)

	RUN_TEST(test_requires);
	RUN_TEST(test_encode_decode);
	RUN_TEST(test_matching_access);

	pr_info("KCSAN selftest: %d/%d tests passed\n", passed, total);
	if (passed != total)
		panic("KCSAN selftests failed");
	return 0;
}
postcore_initcall(kcsan_selftest);
