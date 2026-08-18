// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause

#include <bpf_atomic.h>

#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/spmc.h>

#define TEST_SPMC_THREADS 3
#define TEST_SPMC_STEALERS (TEST_SPMC_THREADS - 1)

/* 
 * The test requires the stealers/owners to sometimes quiesce
 * before continuing the benchmark. Normally we'd use something
 * like a condition variable, but since the benchmark is short-lived
 * and operations are wait-free we just spin around the quiescence
 * point instead. If we time out, we just fail the benchmark.
 */
#define TEST_SPMC_SYNC_SPINS BPF_MAX_LOOPS

/*
 * We track all the values we retrieve from the queue
 * to get some guarantee we're, not corrupting data,
 * e.g., accidentally reusing a past value from a slot.
 */
#define TEST_SPMC_MAX_VALUES (1024)
static u64 __arena seen[TEST_SPMC_MAX_VALUES];

/* The single spmc queue for the benchmark. */
static struct spmc __arena *spmc;

/* Owner and stealer epochs. We define the , */
static volatile u64 owner_epoch;
static volatile u64 stealer_epoch;

/* Map owner epochs to stealer epochs (simply scale by # of stealers). */
#define STEALER_EPOCH(owner_epoch) ((owner_epoch) * TEST_SPMC_STEALERS)

/* Global abort switch. If any thread fails, all others exit ASAP. */
static volatile bool test_abort;

/* 
 * Counters useful for ensuring conservation of pushes/pops of unique values
 * (we're not stealing/popping more/fewer items than were pushed).
 */
static volatile u64 expected_total;
static volatile u64 total_seen;

/* Measure how many pops and steals we've made (irrespective of retrieved value). */
static volatile u64 pops;
static volatile u64 steals;

/* Used for the resize selftest, see below. */
static volatile u64 stealers_started;

/* Used for the mixed selftest, see below. */
static volatile u64 round_steals;

/*
 * We have multiple stealers and a single owner. We sometimes want the owner
 * to successfully outproduce the stealers, we add a busy loop in them.
 */
#define TEST_SPMC_WASTE_ROUNDS (1UL << 12)

/*
 * The spmc data structure depends on the runtime fully
 * supporting acquire/release semantics, which is not
 * the case for all architectures.
 */
#if defined(ENABLE_ATOMICS_TESTS) &&		  \
	(defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
	 (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64))
static bool spmc_tests_enabled(void)
{
	return true;
}
#else
static bool spmc_tests_enabled(void)
{
	return false;
}
#endif

/*
 * Scaffolding for each parallel test. Each test has setup/teardown,
 * a single owner thread that owns the queue, and TEST_SPMC_STEALER
 * threads that try to steal.
 */
#define DEFINE_PARALLEL_SPMC_TEST(prefix, expected_total)		\
	SEC("syscall") int parallel_test_spmc_##prefix##__enabled(void)	\
	{								\
		return spmc_tests_enabled() ? 0 : -EOPNOTSUPP;		\
	}								\
	SEC("syscall") int parallel_test_spmc_##prefix##__init(void)	\
	{								\
		return spmc_common_init(expected_total);		\
	}								\
	SEC("syscall") int parallel_test_spmc_##prefix##__fini(void)	\
	{								\
		return spmc_common_fini();				\
	}								\
	SEC("syscall") int parallel_test_spmc_##prefix##__0(void)	\
	{								\
		return spmc_##prefix##_owner();				\
	}								\
	SEC("syscall") int parallel_test_spmc_##prefix##__1(void)	\
	{								\
		return spmc_##prefix##_stealer();					\
	}								\
	SEC("syscall") int parallel_test_spmc_##prefix##__2(void)	\
	{								\
		return spmc_##prefix##_stealer();					\
	}								\

static int spmc_common_init(u64 total)
{
	u64 i;

	if (total > TEST_SPMC_MAX_VALUES)
		return -E2BIG;

	owner_epoch = 0;
	stealer_epoch = 0;
	test_abort = false;
	expected_total = total;
	total_seen = 0;
	pops = 0;
	steals = 0;
	stealers_started = 0;
	round_steals = 0;

	for (i = zero; i < TEST_SPMC_MAX_VALUES && can_loop; i++)
		seen[i] = 0;

	spmc = spmc_create();
	if (!spmc)
		return -ENOMEM;

	return 0;
}

static int spmc_common_fini(void)
{
	int ret;

	ret = spmc_destroy(spmc);
	spmc = NULL;

	return ret;
}

__weak
int spmc_quiesce_on_owner(u64 epoch)
{
	u64 i;

	bpf_for(i, 0, TEST_SPMC_SYNC_SPINS) {
		if (test_abort)
			return -EINTR;
		if (smp_load_acquire(&owner_epoch) >= epoch)
			return 0;
	}

	test_abort = true;

	return -ETIMEDOUT;
}

__weak
int spmc_quiesce_on_stealer(u64 epoch)
{
	u64 target, cur;
	unsigned int i;
	int err = -ETIMEDOUT;

	target = STEALER_EPOCH(epoch);
	bpf_for(i, 0, TEST_SPMC_SYNC_SPINS) {

		if (test_abort) {
			err = -EINTR;
			break;
		}

		cur = smp_load_acquire(&stealer_epoch);
		if (cur > target) {
			err = -EINVAL;
			test_abort = true;
			break;
		}

		if (cur == target)
			return 0;
	}

	test_abort = true;

	return err;
}

static int spmc_update_stats(u64 val, bool owner)
{
	u64 total;

	total = expected_total;
	if (val >= total || val >= TEST_SPMC_MAX_VALUES) {
		test_abort = true;
		return -EINVAL;
	}

	if (__sync_fetch_and_add(&seen[val], 1) != 0) {
		test_abort = true;
		return -EINVAL;
	}

	__sync_fetch_and_add(&total_seen, 1);
	if (owner)
		__sync_fetch_and_add(&pops, 1);
	else
		__sync_fetch_and_add(&steals, 1);

	return 0;
}

static int spmc_validate_owner_empty(void)
{
	u64 val;
	int ret;

	ret = spmc_owned_remove(spmc, &val);
	if (ret != -ENOENT) {
		test_abort = true;
		/* Change a 0 return value into -EINVAL. */
		return ret ?: -EINVAL;
	}

	return 0;
}

__weak
int spmc_validate_all_seen(void)
{
	u64 i, total;

	total = expected_total;
	if (total_seen != total)
		goto err;

	if (pops + steals != total)
		goto err;

	for (i = zero; i < total && can_loop; i++) {
		if (seen[i % TEST_SPMC_MAX_VALUES] != 1)
			goto err;
	}

	return 0;

err:
	test_abort = true;

	return -EINVAL;
}

/*
 * Single value benchmark. The owner adds an item then races with
 * the stealers for it. This way directly race between owner and
 * stealers on the same slot.
 */


#define TEST_SPMC_SINGLEVAL_ITERS (64)

__weak
int spmc_singleval_tryconsume(u64 expected, bool steal)
{
	u64 val;
	int ret;

	while (can_loop) {
		if (steal)
			ret = spmc_steal(spmc, &val);
		else
			ret = spmc_owned_remove(spmc, &val);

		/* Success. Update and validate. */
		if (!ret) {
			if (val != expected)
				return -EINVAL;

			ret = spmc_update_stats(val, !steal);
			if (ret)
				return ret;

			return 0;
		}

		/*
		 * If we got -ENOENT, the queue is empty
		 * and we're good to go.
		 */
		if (ret != -EAGAIN)
			return (ret == -ENOENT) ? 0 : ret;
	}

	/* Impossible. */
	return -EINVAL;
}

static int spmc_singleval_owner(void)
{
	int ret;
	u64 i;

	for (i = zero; i < TEST_SPMC_SINGLEVAL_ITERS && can_loop; i++) {
		ret = spmc_quiesce_on_stealer(i);
		if (ret)
			goto err;

		ret = spmc_owned_add(spmc, i);
		if (ret)
			goto err;

		__sync_fetch_and_add(&owner_epoch, 1);

		ret = spmc_singleval_tryconsume(i, false);
		if (ret)
			goto err;

		ret = spmc_quiesce_on_stealer(i + 1);
		if (ret)
			goto err;
	}

	ret = spmc_validate_owner_empty();
	if (ret)
		return ret;

	return spmc_validate_all_seen();

err:
	test_abort = true;
	return -EINVAL;
}

static int spmc_singleval_stealer(void)
{
	int ret;
	u64 i;

	for (i = zero; i < TEST_SPMC_SINGLEVAL_ITERS && can_loop; i++) {
		ret = spmc_quiesce_on_owner(i + 1);
		if (ret)
			goto err;

		ret = spmc_singleval_tryconsume(i, true);
		if (ret)
			goto err;

		__sync_fetch_and_add(&stealer_epoch, 1);
	}

	return 0;

err:
	test_abort = true;
	return -EINVAL;
}

DEFINE_PARALLEL_SPMC_TEST(singleval, TEST_SPMC_SINGLEVAL_ITERS)

/*
 * The resize test. Force a resize from the owner even while the stealers
 * are trying to consume. Then make sure the queue is still consistent
 * after the resize.
 *
 * The owner _doesn't_ consume from the queue. The test makes sure that
 * switching the array from underneath the stealers works.
 */

/* Force 2 resizes (since the rate of resize is logarithmic). */
#define TEST_SPMC_RESIZE_ORDER (2)
#define TEST_SPMC_RESIZE_PREFILL ((SPMC_ARR_BASESZ << TEST_SPMC_RESIZE_ORDER) - 1)

/* */
#define TEST_SPMC_RESIZE_TAIL (SPMC_ARR_BASESZ << TEST_SPMC_RESIZE_ORDER)
#define TEST_SPMC_RESIZE_TOTAL (TEST_SPMC_RESIZE_PREFILL + TEST_SPMC_RESIZE_TAIL)

__weak
int spmc_wait_for_stealers_to_start(u64 target)
{
	u64 i;

	bpf_for(i, 0, TEST_SPMC_SYNC_SPINS) {
		if (test_abort)
			return -EINTR;
		if (READ_ONCE(stealers_started) >= target)
			return 0;
	}

	test_abort = true;

	return -ETIMEDOUT;
}

__weak
void spmc_waste_time(void)
{
	int i;
	int j;

	for (i = zero; i < TEST_SPMC_WASTE_ROUNDS && can_loop; i++) {
		/* Random computation. */
		WRITE_ONCE(j, i * 17 + 23);
	}
}

static int spmc_resize_owner(void)
{
	bool resized = false;
	u64 i;
	int ret;

	/* Get a head start vs the consumers. */
	for (i = zero; i < TEST_SPMC_RESIZE_PREFILL && can_loop; i++) {
		ret = spmc_owned_add(spmc, i);
		if (ret) {
			test_abort = true;
			return ret;
		}
	}

	__sync_fetch_and_add(&owner_epoch, 1);

	/* Wait for stealers to start then start racing. */
	ret = spmc_wait_for_stealers_to_start(TEST_SPMC_STEALERS);
	if (ret)
		return ret;

	for (i = TEST_SPMC_RESIZE_PREFILL; i < TEST_SPMC_RESIZE_TOTAL && can_loop; i++) {
		ret = spmc_owned_add(spmc, i);
		if (ret) {
			test_abort = true;
			return ret;
		}

		if (spmc->cur->order > TEST_SPMC_RESIZE_ORDER)
			resized = true;
	}

	/* Did we get to resize while racing? */
	if (!resized) {
		test_abort = true;
		return -EINVAL;
	}

	/* 
	 * Wait for the stealers to drain and make sure
	 * we didn't lose any items along the way.
	 */
	__sync_fetch_and_add(&owner_epoch, 1);

	ret = spmc_quiesce_on_stealer(1);
	if (ret)
		return ret;

	ret = spmc_validate_owner_empty();
	if (ret)
		return ret;

	return spmc_validate_all_seen();
}

static int spmc_resize_stealer(void)
{
	bool owner_done = false;
	u64 val;
	int ret;

	arena_subprog_init();

	ret = spmc_quiesce_on_owner(1);
	if (ret)
		return ret;

	__sync_fetch_and_add(&stealers_started, 1);

	while (can_loop) {
		spmc_waste_time();
		if (test_abort)
			return -EINTR;

		ret = spmc_steal(spmc, &val);
		if (!ret) {
			ret = spmc_update_stats(val, false);
			if (ret)
				return ret;
			continue;
		}

		if (ret == -EAGAIN)
			continue;

		if (ret == -ENOENT) {
			if (owner_done)
				break;
			owner_done = owner_epoch >= 2;
			continue;
		}

		test_abort = true;
		return ret;
	}

	__sync_fetch_and_add(&stealer_epoch, 1);

	return 0;
}

DEFINE_PARALLEL_SPMC_TEST(resize, TEST_SPMC_RESIZE_TOTAL)

/*
 * The burst benchmark. The owner generates data all at once,
 * then waits for the stealers to steal half then starts removing 
 * items until the queue empties. The owner also makes sure the
 * item order is not jumbled.
 */

#define TEST_SPMC_BURST_ROUNDS (4)
#define TEST_SPMC_BURST_BURST (64)
#define TEST_SPMC_BURST_TOTAL (TEST_SPMC_BURST_ROUNDS * TEST_SPMC_BURST_BURST)
#define TEST_SPMC_BURST_STEAL_TARGET (TEST_SPMC_BURST_BURST / 2)

static int spmc_wait_for_round_steals(u64 target)
{
	u64 i;

	arena_subprog_init();

	bpf_for(i, 0, TEST_SPMC_SYNC_SPINS) {
		if (test_abort)
			return -EINTR;
		if (round_steals >= target)
			return 0;
	}

	test_abort = true;

	return -ETIMEDOUT;
}

__weak int
spmc_burst_owner_round(u64 round)
{
	u64 i, base, stolen, expected, val;
	int ret;

	base = round * TEST_SPMC_BURST_BURST;
	round_steals = 0;

	for (i = zero; i < TEST_SPMC_BURST_BURST && can_loop; i++) {
		ret = spmc_owned_add(spmc, base + i);
		if (ret)
			return ret;
	}

	__sync_fetch_and_add(&owner_epoch, 1);

	ret = spmc_wait_for_round_steals(TEST_SPMC_BURST_STEAL_TARGET);
	if (ret == -EINTR || ret == -ETIMEDOUT)
		return ret;

	__sync_fetch_and_add(&owner_epoch, 1);

	ret = spmc_quiesce_on_stealer(round + 1);
	if (ret)
		return ret;

	stolen = round_steals;
	if (stolen > TEST_SPMC_BURST_BURST)
		return -EINVAL;

	for (i = zero; i < TEST_SPMC_BURST_BURST - stolen && can_loop; i++) {
		ret = spmc_owned_remove(spmc, &val);
		if (ret)
			return ret;

		expected = base + TEST_SPMC_BURST_BURST - 1 - i;
		if (val != expected)
			return -EINVAL;

		ret = spmc_update_stats(val, true);
		if (ret) {
			test_abort = true;
			return -EINVAL;
		}
	}

	ret = spmc_validate_owner_empty();
	if (ret)
		return ret;

	return 0;
}

static int spmc_burst_owner(void)
{
	u64 round;
	int ret;

	arena_subprog_init();

	for (round = zero; round < TEST_SPMC_BURST_ROUNDS && can_loop; round++) {
		ret = spmc_burst_owner_round(round);
		if (ret)
			goto err;
	}

	return spmc_validate_all_seen();

err:
	test_abort = true;
	return -EINVAL;
}

static int spmc_burst_stealer(void)
{
	u64 round, val, active_epoch;
	int ret;

	arena_subprog_init();

	for (round = zero; round < TEST_SPMC_BURST_ROUNDS && can_loop; round++) {
		active_epoch = round * 2 + 1;

		/* 
		 * Wait till the owner prefills the queue then
		 * start stealing.
		 */
		ret = spmc_quiesce_on_owner(active_epoch);
		if (ret)
			return ret;

		while (owner_epoch == active_epoch && can_loop) {
			if (test_abort)
				return -EINTR;

			ret = spmc_steal(spmc, &val);
			if (!ret) {
				ret = spmc_update_stats(val, false);
				if (ret)
					return ret;
				__sync_fetch_and_add(&round_steals, 1);
				continue;
			}
			if (ret == -EAGAIN || ret == -ENOENT)
				continue;

			test_abort = true;
			return ret;
		}

		__sync_fetch_and_add(&stealer_epoch, 1);
	}

	return 0;
}

DEFINE_PARALLEL_SPMC_TEST(burst, TEST_SPMC_BURST_TOTAL)
