/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/types.h>

struct primes {
	struct rcu_head rcu;
	unsigned long last, sz;
	unsigned long primes[];
};

#if IS_ENABLED(CONFIG_PRIME_NUMBERS_KUNIT_TEST)
typedef void (*primes_fn)(void *, const struct primes *);

void with_primes(void *ctx, primes_fn fn);
bool slow_is_prime_number(unsigned long x);
#endif
