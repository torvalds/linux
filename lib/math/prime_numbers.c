// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/prime_numbers.h>
#include <linux/slab.h>

#include "prime_numbers_private.h"

#if BITS_PER_LONG == 64
static const struct primes small_primes = {
	.last = 61,
	.sz = 64,
	.primes = {
		BIT(2) |
		BIT(3) |
		BIT(5) |
		BIT(7) |
		BIT(11) |
		BIT(13) |
		BIT(17) |
		BIT(19) |
		BIT(23) |
		BIT(29) |
		BIT(31) |
		BIT(37) |
		BIT(41) |
		BIT(43) |
		BIT(47) |
		BIT(53) |
		BIT(59) |
		BIT(61)
	}
};
#elif BITS_PER_LONG == 32
static const struct primes small_primes = {
	.last = 31,
	.sz = 32,
	.primes = {
		BIT(2) |
		BIT(3) |
		BIT(5) |
		BIT(7) |
		BIT(11) |
		BIT(13) |
		BIT(17) |
		BIT(19) |
		BIT(23) |
		BIT(29) |
		BIT(31)
	}
};
#else
#error "unhandled BITS_PER_LONG"
#endif

static DEFINE_MUTEX(lock);
static const struct primes __rcu *primes = RCU_INITIALIZER(&small_primes);

#if IS_ENABLED(CONFIG_PRIME_NUMBERS_KUNIT_TEST)
/*
 * Calls the callback under RCU lock. The callback must not retain
 * the primes pointer.
 */
void with_primes(void *ctx, primes_fn fn)
{
	rcu_read_lock();
	fn(ctx, rcu_dereference(primes));
	rcu_read_unlock();
}
EXPORT_SYMBOL(with_primes);

EXPORT_SYMBOL(slow_is_prime_number);

#else
static
#endif
bool slow_is_prime_number(unsigned long x)
{
	unsigned long y = int_sqrt(x);

	while (y > 1) {
		if ((x % y) == 0)
			break;
		y--;
	}

	return y == 1;
}

static unsigned long slow_next_prime_number(unsigned long x)
{
	while (x < ULONG_MAX && !slow_is_prime_number(++x))
		;

	return x;
}

static unsigned long clear_multiples(unsigned long x,
				     unsigned long *p,
				     unsigned long start,
				     unsigned long end)
{
	unsigned long m;

	m = 2 * x;
	if (m < start)
		m = roundup(start, x);

	while (m < end) {
		__clear_bit(m, p);
		m += x;
	}

	return x;
}

static bool expand_to_next_prime(unsigned long x)
{
	const struct primes *p;
	struct primes *new;
	unsigned long sz, y;

	/* Betrand's Postulate (or Chebyshev's theorem) states that if n > 3,
	 * there is always at least one prime p between n and 2n - 2.
	 * Equivalently, if n > 1, then there is always at least one prime p
	 * such that n < p < 2n.
	 *
	 * http://mathworld.wolfram.com/BertrandsPostulate.html
	 * https://en.wikipedia.org/wiki/Bertrand's_postulate
	 */
	sz = 2 * x;
	if (sz < x)
		return false;

	sz = round_up(sz, BITS_PER_LONG);
	new = kmalloc(sizeof(*new) + bitmap_size(sz),
		      GFP_KERNEL | __GFP_NOWARN);
	if (!new)
		return false;

	mutex_lock(&lock);
	p = rcu_dereference_protected(primes, lockdep_is_held(&lock));
	if (x < p->last) {
		kfree(new);
		goto unlock;
	}

	/* Where memory permits, track the primes using the
	 * Sieve of Eratosthenes. The sieve is to remove all multiples of known
	 * primes from the set, what remains in the set is therefore prime.
	 */
	bitmap_fill(new->primes, sz);
	bitmap_copy(new->primes, p->primes, p->sz);
	for (y = 2UL; y < sz; y = find_next_bit(new->primes, sz, y + 1))
		new->last = clear_multiples(y, new->primes, p->sz, sz);
	new->sz = sz;

	BUG_ON(new->last <= x);

	rcu_assign_pointer(primes, new);
	if (p != &small_primes)
		kfree_rcu((struct primes *)p, rcu);

unlock:
	mutex_unlock(&lock);
	return true;
}

static void free_primes(void)
{
	const struct primes *p;

	mutex_lock(&lock);
	p = rcu_dereference_protected(primes, lockdep_is_held(&lock));
	if (p != &small_primes) {
		rcu_assign_pointer(primes, &small_primes);
		kfree_rcu((struct primes *)p, rcu);
	}
	mutex_unlock(&lock);
}

/**
 * next_prime_number - return the next prime number
 * @x: the starting point for searching to test
 *
 * A prime number is an integer greater than 1 that is only divisible by
 * itself and 1.  The set of prime numbers is computed using the Sieve of
 * Eratoshenes (on finding a prime, all multiples of that prime are removed
 * from the set) enabling a fast lookup of the next prime number larger than
 * @x. If the sieve fails (memory limitation), the search falls back to using
 * slow trial-divison, up to the value of ULONG_MAX (which is reported as the
 * final prime as a sentinel).
 *
 * Returns: the next prime number larger than @x
 */
unsigned long next_prime_number(unsigned long x)
{
	const struct primes *p;

	rcu_read_lock();
	p = rcu_dereference(primes);
	while (x >= p->last) {
		rcu_read_unlock();

		if (!expand_to_next_prime(x))
			return slow_next_prime_number(x);

		rcu_read_lock();
		p = rcu_dereference(primes);
	}
	x = find_next_bit(p->primes, p->last, x + 1);
	rcu_read_unlock();

	return x;
}
EXPORT_SYMBOL(next_prime_number);

/**
 * is_prime_number - test whether the given number is prime
 * @x: the number to test
 *
 * A prime number is an integer greater than 1 that is only divisible by
 * itself and 1. Internally a cache of prime numbers is kept (to speed up
 * searching for sequential primes, see next_prime_number()), but if the number
 * falls outside of that cache, its primality is tested using trial-divison.
 *
 * Returns: true if @x is prime, false for composite numbers.
 */
bool is_prime_number(unsigned long x)
{
	const struct primes *p;
	bool result;

	rcu_read_lock();
	p = rcu_dereference(primes);
	while (x >= p->sz) {
		rcu_read_unlock();

		if (!expand_to_next_prime(x))
			return slow_is_prime_number(x);

		rcu_read_lock();
		p = rcu_dereference(primes);
	}
	result = test_bit(x, p->primes);
	rcu_read_unlock();

	return result;
}
EXPORT_SYMBOL(is_prime_number);

static void __exit primes_exit(void)
{
	free_primes();
}

module_exit(primes_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Prime number library");
MODULE_LICENSE("GPL");
