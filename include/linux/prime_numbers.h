/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PRIME_NUMBERS_H
#define __LINUX_PRIME_NUMBERS_H

#include <linux/types.h>

bool is_prime_number(unsigned long x);
unsigned long next_prime_number(unsigned long x);

/**
 * for_each_prime_number - iterate over each prime upto a value
 * @prime: the current prime number in this iteration
 * @max: the upper limit
 *
 * Starting from the first prime number 2 iterate over each prime number up to
 * the @max value. On each iteration, @prime is set to the current prime number.
 * @max should be less than ULONG_MAX to ensure termination. To begin with
 * @prime set to 1 on the first iteration use for_each_prime_number_from()
 * instead.
 */
#define for_each_prime_number(prime, max) \
	for_each_prime_number_from((prime), 2, (max))

/**
 * for_each_prime_number_from - iterate over each prime upto a value
 * @prime: the current prime number in this iteration
 * @from: the initial value
 * @max: the upper limit
 *
 * Starting from @from iterate over each successive prime number up to the
 * @max value. On each iteration, @prime is set to the current prime number.
 * @max should be less than ULONG_MAX, and @from less than @max, to ensure
 * termination.
 */
#define for_each_prime_number_from(prime, from, max) \
	for (prime = (from); prime <= (max); prime = next_prime_number(prime))

#endif /* !__LINUX_PRIME_NUMBERS_H */
