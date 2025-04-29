// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/module.h>
#include <linux/prime_numbers.h>

#include "../prime_numbers_private.h"

static void dump_primes(void *ctx, const struct primes *p)
{
	static char buf[PAGE_SIZE];
	struct kunit_suite *suite = ctx;

	bitmap_print_to_pagebuf(true, buf, p->primes, p->sz);
	kunit_info(suite, "primes.{last=%lu, .sz=%lu, .primes[]=...x%lx} = %s",
		   p->last, p->sz, p->primes[BITS_TO_LONGS(p->sz) - 1], buf);
}

static void prime_numbers_test(struct kunit *test)
{
	const unsigned long max = 65536;
	unsigned long x, last, next;

	for (last = 0, x = 2; x < max; x++) {
		const bool slow = slow_is_prime_number(x);
		const bool fast = is_prime_number(x);

		KUNIT_ASSERT_EQ_MSG(test, slow, fast, "is-prime(%lu)", x);

		if (!slow)
			continue;

		next = next_prime_number(last);
		KUNIT_ASSERT_EQ_MSG(test, next, x, "next-prime(%lu)", last);
		last = next;
	}
}

static void kunit_suite_exit(struct kunit_suite *suite)
{
	with_primes(suite, dump_primes);
}

static struct kunit_case prime_numbers_cases[] = {
	KUNIT_CASE(prime_numbers_test),
	{},
};

static struct kunit_suite prime_numbers_suite = {
	.name = "math-prime_numbers",
	.suite_exit = kunit_suite_exit,
	.test_cases = prime_numbers_cases,
};

kunit_test_suite(prime_numbers_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Prime number library");
MODULE_LICENSE("GPL");
