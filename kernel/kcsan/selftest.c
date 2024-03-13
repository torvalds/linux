// SPDX-License-Identifier: GPL-2.0
/*
 * KCSAN short boot-time selftests.
 *
 * Copyright (C) 2019, Google LLC.
 */

#define pr_fmt(fmt) "kcsan: " fmt

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kcsan-checks.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "encoding.h"

#define ITERS_PER_TEST 2000

/* Test requirements. */
static bool __init test_requires(void)
{
	/* random should be initialized for the below tests */
	return get_random_u32() + get_random_u32() != 0;
}

/*
 * Test watchpoint encode and decode: check that encoding some access's info,
 * and then subsequent decode preserves the access's info.
 */
static bool __init test_encode_decode(void)
{
	int i;

	for (i = 0; i < ITERS_PER_TEST; ++i) {
		size_t size = prandom_u32_max(MAX_ENCODABLE_SIZE) + 1;
		bool is_write = !!prandom_u32_max(2);
		unsigned long verif_masked_addr;
		long encoded_watchpoint;
		bool verif_is_write;
		unsigned long addr;
		size_t verif_size;

		get_random_bytes(&addr, sizeof(addr));
		if (addr < PAGE_SIZE)
			addr = PAGE_SIZE;

		if (WARN_ON(!check_encodable(addr, size)))
			return false;

		encoded_watchpoint = encode_watchpoint(addr, size, is_write);

		/* Check special watchpoints */
		if (WARN_ON(decode_watchpoint(INVALID_WATCHPOINT, &verif_masked_addr, &verif_size, &verif_is_write)))
			return false;
		if (WARN_ON(decode_watchpoint(CONSUMED_WATCHPOINT, &verif_masked_addr, &verif_size, &verif_is_write)))
			return false;

		/* Check decoding watchpoint returns same data */
		if (WARN_ON(!decode_watchpoint(encoded_watchpoint, &verif_masked_addr, &verif_size, &verif_is_write)))
			return false;
		if (WARN_ON(verif_masked_addr != (addr & WATCHPOINT_ADDR_MASK)))
			goto fail;
		if (WARN_ON(verif_size != size))
			goto fail;
		if (WARN_ON(is_write != verif_is_write))
			goto fail;

		continue;
fail:
		pr_err("%s fail: %s %zu bytes @ %lx -> encoded: %lx -> %s %zu bytes @ %lx\n",
		       __func__, is_write ? "write" : "read", size, addr, encoded_watchpoint,
		       verif_is_write ? "write" : "read", verif_size, verif_masked_addr);
		return false;
	}

	return true;
}

/* Test access matching function. */
static bool __init test_matching_access(void)
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

/*
 * Correct memory barrier instrumentation is critical to avoiding false
 * positives: simple test to check at boot certain barriers are always properly
 * instrumented. See kcsan_test for a more complete test.
 */
static DEFINE_SPINLOCK(test_spinlock);
static bool __init test_barrier(void)
{
#ifdef CONFIG_KCSAN_WEAK_MEMORY
	struct kcsan_scoped_access *reorder_access = &current->kcsan_ctx.reorder_access;
#else
	struct kcsan_scoped_access *reorder_access = NULL;
#endif
	bool ret = true;
	arch_spinlock_t arch_spinlock = __ARCH_SPIN_LOCK_UNLOCKED;
	atomic_t dummy;
	long test_var;

	if (!reorder_access || !IS_ENABLED(CONFIG_SMP))
		return true;

#define __KCSAN_CHECK_BARRIER(access_type, barrier, name)					\
	do {											\
		reorder_access->type = (access_type) | KCSAN_ACCESS_SCOPED;			\
		reorder_access->size = 1;							\
		barrier;									\
		if (reorder_access->size != 0) {						\
			pr_err("improperly instrumented type=(" #access_type "): " name "\n");	\
			ret = false;								\
		}										\
	} while (0)
#define KCSAN_CHECK_READ_BARRIER(b)  __KCSAN_CHECK_BARRIER(0, b, #b)
#define KCSAN_CHECK_WRITE_BARRIER(b) __KCSAN_CHECK_BARRIER(KCSAN_ACCESS_WRITE, b, #b)
#define KCSAN_CHECK_RW_BARRIER(b)    __KCSAN_CHECK_BARRIER(KCSAN_ACCESS_WRITE | KCSAN_ACCESS_COMPOUND, b, #b)

	kcsan_nestable_atomic_begin(); /* No watchpoints in called functions. */

	KCSAN_CHECK_READ_BARRIER(mb());
	KCSAN_CHECK_READ_BARRIER(rmb());
	KCSAN_CHECK_READ_BARRIER(smp_mb());
	KCSAN_CHECK_READ_BARRIER(smp_rmb());
	KCSAN_CHECK_READ_BARRIER(dma_rmb());
	KCSAN_CHECK_READ_BARRIER(smp_mb__before_atomic());
	KCSAN_CHECK_READ_BARRIER(smp_mb__after_atomic());
	KCSAN_CHECK_READ_BARRIER(smp_mb__after_spinlock());
	KCSAN_CHECK_READ_BARRIER(smp_store_mb(test_var, 0));
	KCSAN_CHECK_READ_BARRIER(smp_store_release(&test_var, 0));
	KCSAN_CHECK_READ_BARRIER(xchg(&test_var, 0));
	KCSAN_CHECK_READ_BARRIER(xchg_release(&test_var, 0));
	KCSAN_CHECK_READ_BARRIER(cmpxchg(&test_var, 0,  0));
	KCSAN_CHECK_READ_BARRIER(cmpxchg_release(&test_var, 0,  0));
	KCSAN_CHECK_READ_BARRIER(atomic_set_release(&dummy, 0));
	KCSAN_CHECK_READ_BARRIER(atomic_add_return(1, &dummy));
	KCSAN_CHECK_READ_BARRIER(atomic_add_return_release(1, &dummy));
	KCSAN_CHECK_READ_BARRIER(atomic_fetch_add(1, &dummy));
	KCSAN_CHECK_READ_BARRIER(atomic_fetch_add_release(1, &dummy));
	KCSAN_CHECK_READ_BARRIER(test_and_set_bit(0, &test_var));
	KCSAN_CHECK_READ_BARRIER(test_and_clear_bit(0, &test_var));
	KCSAN_CHECK_READ_BARRIER(test_and_change_bit(0, &test_var));
	KCSAN_CHECK_READ_BARRIER(clear_bit_unlock(0, &test_var));
	KCSAN_CHECK_READ_BARRIER(__clear_bit_unlock(0, &test_var));
	arch_spin_lock(&arch_spinlock);
	KCSAN_CHECK_READ_BARRIER(arch_spin_unlock(&arch_spinlock));
	spin_lock(&test_spinlock);
	KCSAN_CHECK_READ_BARRIER(spin_unlock(&test_spinlock));

	KCSAN_CHECK_WRITE_BARRIER(mb());
	KCSAN_CHECK_WRITE_BARRIER(wmb());
	KCSAN_CHECK_WRITE_BARRIER(smp_mb());
	KCSAN_CHECK_WRITE_BARRIER(smp_wmb());
	KCSAN_CHECK_WRITE_BARRIER(dma_wmb());
	KCSAN_CHECK_WRITE_BARRIER(smp_mb__before_atomic());
	KCSAN_CHECK_WRITE_BARRIER(smp_mb__after_atomic());
	KCSAN_CHECK_WRITE_BARRIER(smp_mb__after_spinlock());
	KCSAN_CHECK_WRITE_BARRIER(smp_store_mb(test_var, 0));
	KCSAN_CHECK_WRITE_BARRIER(smp_store_release(&test_var, 0));
	KCSAN_CHECK_WRITE_BARRIER(xchg(&test_var, 0));
	KCSAN_CHECK_WRITE_BARRIER(xchg_release(&test_var, 0));
	KCSAN_CHECK_WRITE_BARRIER(cmpxchg(&test_var, 0,  0));
	KCSAN_CHECK_WRITE_BARRIER(cmpxchg_release(&test_var, 0,  0));
	KCSAN_CHECK_WRITE_BARRIER(atomic_set_release(&dummy, 0));
	KCSAN_CHECK_WRITE_BARRIER(atomic_add_return(1, &dummy));
	KCSAN_CHECK_WRITE_BARRIER(atomic_add_return_release(1, &dummy));
	KCSAN_CHECK_WRITE_BARRIER(atomic_fetch_add(1, &dummy));
	KCSAN_CHECK_WRITE_BARRIER(atomic_fetch_add_release(1, &dummy));
	KCSAN_CHECK_WRITE_BARRIER(test_and_set_bit(0, &test_var));
	KCSAN_CHECK_WRITE_BARRIER(test_and_clear_bit(0, &test_var));
	KCSAN_CHECK_WRITE_BARRIER(test_and_change_bit(0, &test_var));
	KCSAN_CHECK_WRITE_BARRIER(clear_bit_unlock(0, &test_var));
	KCSAN_CHECK_WRITE_BARRIER(__clear_bit_unlock(0, &test_var));
	arch_spin_lock(&arch_spinlock);
	KCSAN_CHECK_WRITE_BARRIER(arch_spin_unlock(&arch_spinlock));
	spin_lock(&test_spinlock);
	KCSAN_CHECK_WRITE_BARRIER(spin_unlock(&test_spinlock));

	KCSAN_CHECK_RW_BARRIER(mb());
	KCSAN_CHECK_RW_BARRIER(wmb());
	KCSAN_CHECK_RW_BARRIER(rmb());
	KCSAN_CHECK_RW_BARRIER(smp_mb());
	KCSAN_CHECK_RW_BARRIER(smp_wmb());
	KCSAN_CHECK_RW_BARRIER(smp_rmb());
	KCSAN_CHECK_RW_BARRIER(dma_wmb());
	KCSAN_CHECK_RW_BARRIER(dma_rmb());
	KCSAN_CHECK_RW_BARRIER(smp_mb__before_atomic());
	KCSAN_CHECK_RW_BARRIER(smp_mb__after_atomic());
	KCSAN_CHECK_RW_BARRIER(smp_mb__after_spinlock());
	KCSAN_CHECK_RW_BARRIER(smp_store_mb(test_var, 0));
	KCSAN_CHECK_RW_BARRIER(smp_store_release(&test_var, 0));
	KCSAN_CHECK_RW_BARRIER(xchg(&test_var, 0));
	KCSAN_CHECK_RW_BARRIER(xchg_release(&test_var, 0));
	KCSAN_CHECK_RW_BARRIER(cmpxchg(&test_var, 0,  0));
	KCSAN_CHECK_RW_BARRIER(cmpxchg_release(&test_var, 0,  0));
	KCSAN_CHECK_RW_BARRIER(atomic_set_release(&dummy, 0));
	KCSAN_CHECK_RW_BARRIER(atomic_add_return(1, &dummy));
	KCSAN_CHECK_RW_BARRIER(atomic_add_return_release(1, &dummy));
	KCSAN_CHECK_RW_BARRIER(atomic_fetch_add(1, &dummy));
	KCSAN_CHECK_RW_BARRIER(atomic_fetch_add_release(1, &dummy));
	KCSAN_CHECK_RW_BARRIER(test_and_set_bit(0, &test_var));
	KCSAN_CHECK_RW_BARRIER(test_and_clear_bit(0, &test_var));
	KCSAN_CHECK_RW_BARRIER(test_and_change_bit(0, &test_var));
	KCSAN_CHECK_RW_BARRIER(clear_bit_unlock(0, &test_var));
	KCSAN_CHECK_RW_BARRIER(__clear_bit_unlock(0, &test_var));
	arch_spin_lock(&arch_spinlock);
	KCSAN_CHECK_RW_BARRIER(arch_spin_unlock(&arch_spinlock));
	spin_lock(&test_spinlock);
	KCSAN_CHECK_RW_BARRIER(spin_unlock(&test_spinlock));

#ifdef clear_bit_unlock_is_negative_byte
	KCSAN_CHECK_RW_BARRIER(clear_bit_unlock_is_negative_byte(0, &test_var));
	KCSAN_CHECK_READ_BARRIER(clear_bit_unlock_is_negative_byte(0, &test_var));
	KCSAN_CHECK_WRITE_BARRIER(clear_bit_unlock_is_negative_byte(0, &test_var));
#endif
	kcsan_nestable_atomic_end();

	return ret;
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
			pr_err("selftest: " #do_test " failed");               \
	} while (0)

	RUN_TEST(test_requires);
	RUN_TEST(test_encode_decode);
	RUN_TEST(test_matching_access);
	RUN_TEST(test_barrier);

	pr_info("selftest: %d/%d tests passed\n", passed, total);
	if (passed != total)
		panic("selftests failed");
	return 0;
}
postcore_initcall(kcsan_selftest);
