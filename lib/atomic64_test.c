/*
 * Testsuite for atomic64_t functions
 *
 * Copyright © 2010  Luca Barbieri
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/init.h>
#include <asm/atomic.h>

#define INIT(c) do { atomic64_set(&v, c); r = c; } while (0)
static __init int test_atomic64(void)
{
	long long v0 = 0xaaa31337c001d00dLL;
	long long v1 = 0xdeadbeefdeafcafeLL;
	long long v2 = 0xfaceabadf00df001LL;
	long long onestwos = 0x1111111122222222LL;
	long long one = 1LL;

	atomic64_t v = ATOMIC64_INIT(v0);
	long long r = v0;
	BUG_ON(v.counter != r);

	atomic64_set(&v, v1);
	r = v1;
	BUG_ON(v.counter != r);
	BUG_ON(atomic64_read(&v) != r);

	INIT(v0);
	atomic64_add(onestwos, &v);
	r += onestwos;
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_add(-one, &v);
	r += -one;
	BUG_ON(v.counter != r);

	INIT(v0);
	r += onestwos;
	BUG_ON(atomic64_add_return(onestwos, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	r += -one;
	BUG_ON(atomic64_add_return(-one, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_sub(onestwos, &v);
	r -= onestwos;
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_sub(-one, &v);
	r -= -one;
	BUG_ON(v.counter != r);

	INIT(v0);
	r -= onestwos;
	BUG_ON(atomic64_sub_return(onestwos, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	r -= -one;
	BUG_ON(atomic64_sub_return(-one, &v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_inc(&v);
	r += one;
	BUG_ON(v.counter != r);

	INIT(v0);
	r += one;
	BUG_ON(atomic64_inc_return(&v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	atomic64_dec(&v);
	r -= one;
	BUG_ON(v.counter != r);

	INIT(v0);
	r -= one;
	BUG_ON(atomic64_dec_return(&v) != r);
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_xchg(&v, v1) != v0);
	r = v1;
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_cmpxchg(&v, v0, v1) != v0);
	r = v1;
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_cmpxchg(&v, v2, v1) != v0);
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(!atomic64_add_unless(&v, one, v0));
	BUG_ON(v.counter != r);

	INIT(v0);
	BUG_ON(atomic64_add_unless(&v, one, v1));
	r += one;
	BUG_ON(v.counter != r);

#if defined(CONFIG_X86_32) || defined(CONFIG_MIPS) || defined(CONFIG_PPC) || defined(_ASM_GENERIC_ATOMIC64_H)
	INIT(onestwos);
	BUG_ON(atomic64_dec_if_positive(&v) != (onestwos - 1));
	r -= one;
	BUG_ON(v.counter != r);

	INIT(0);
	BUG_ON(atomic64_dec_if_positive(&v) != -one);
	BUG_ON(v.counter != r);

	INIT(-one);
	BUG_ON(atomic64_dec_if_positive(&v) != (-one - one));
	BUG_ON(v.counter != r);
#else
#warning Please implement atomic64_dec_if_positive for your architecture, and add it to the IF above
#endif

	INIT(onestwos);
	BUG_ON(atomic64_inc_not_zero(&v));
	r += one;
	BUG_ON(v.counter != r);

	INIT(0);
	BUG_ON(!atomic64_inc_not_zero(&v));
	BUG_ON(v.counter != r);

	INIT(-one);
	BUG_ON(atomic64_inc_not_zero(&v));
	r += one;
	BUG_ON(v.counter != r);

#ifdef CONFIG_X86
	printk(KERN_INFO "atomic64 test passed for %s+ platform %s CX8 and %s SSE\n",
#ifdef CONFIG_X86_CMPXCHG64
			"586",
#else
			"386",
#endif
			boot_cpu_has(X86_FEATURE_CX8) ? "with" : "without",
			boot_cpu_has(X86_FEATURE_XMM) ? "with" : "without");
#else
	printk(KERN_INFO "atomic64 test passed\n");
#endif

	return 0;
}

core_initcall(test_atomic64);
