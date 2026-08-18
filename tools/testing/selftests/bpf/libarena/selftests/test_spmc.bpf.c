// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause

#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/spmc.h>

/*
 * NOTE: These selftests only test for the single-threaded use case, which for
 * Lev-Chase queues is obviously the simplest one. Still, it is important to
 * exercise the API to ensure it passes verification and basic checks.
 */

SEC("syscall")
int test_spmc_remove_empty(void)
{
	u64 val;
	int ret;

	struct spmc __arena *spmc = spmc_create();

	if (!spmc)
		return 1;

	ret = spmc_owned_remove(spmc, &val);
	if (ret != -ENOENT)
		return 1;

	spmc_destroy(spmc);

	return 0;
}

SEC("syscall")
int test_spmc_steal_empty(void)
{
	u64 val;
	int ret;

	struct spmc __arena *spmc = spmc_create();

	if (!spmc)
		return 1;

	ret = spmc_steal(spmc, &val);
	if (ret != -ENOENT)
		return 1;

	spmc_destroy(spmc);

	return 0;
}

SEC("syscall")
int test_spmc_steal_one(void)
{
	u64 val, newval;
	int ret, i;

	struct spmc __arena *spmc = spmc_create();

	if (!spmc)
		return 1;

	for (i = 0; i < 10 && can_loop; i++) {
		val = i;

		ret = spmc_owned_add(spmc, val);
		if (ret)
			return 1;

		ret = spmc_steal(spmc, &newval);
		if (ret)
			return 2;

		if (val != newval)
			return 3;
	}

	spmc_destroy(spmc);

	return 0;
}

SEC("syscall")
int test_spmc_remove_one(void)
{
	u64 val, newval;
	int ret, i;

	struct spmc __arena *spmc = spmc_create();

	if (!spmc)
		return 1;

	for (i = 0; i < 10 && can_loop; i++) {
		val = i;

		ret = spmc_owned_add(spmc, val);
		if (ret)
			return 1;

		ret = spmc_owned_remove(spmc, &newval);
		if (ret)
			return 2;

		if (val != newval)
			return 3;
	}

	spmc_destroy(spmc);

	return 0;
}

SEC("syscall")
int test_spmc_remove_many(void)
{
	u64 val, newval;
	int ret, i;
	u64 expected;

	struct spmc __arena *spmc = spmc_create();

	if (!spmc)
		return 1;

	for (i = 0; i < 500 && can_loop; i++) {
		val = i;

		ret = spmc_owned_add(spmc, val);
		if (ret) {
			arena_stderr("%s:%d error %d\n", __func__, __LINE__, ret);
			return 1;
		}
	}

	for (i = 0; i < 500 && can_loop; i++) {
		ret = spmc_owned_remove(spmc, &newval);
		if (ret) {
			arena_stderr("%s:%d error %d\n", __func__, __LINE__, ret);
			return 1;
		}

		expected = 500 - 1 - i;
		if (newval != expected) {
			arena_stderr("%s:%d expected %llu found %llu\n", __func__, __LINE__, expected, newval);
			return 1;
		}
	}

	spmc_destroy(spmc);

	return 0;
}

SEC("syscall")
int test_spmc_steal_many(void)
{
	u64 val, newval;
	int ret, i;

	struct spmc __arena *spmc = spmc_create();

	if (!spmc)
		return 1;

	for (i = 0; i < 500 && can_loop; i++) {
		val = i;

		ret = spmc_owned_add(spmc, val);
		if (ret) {
			arena_stderr("%s:%d error %d\n", __func__, __LINE__, ret);
			return 1;
		}
	}

	for (i = 0; i < 500 && can_loop; i++) {
		ret = spmc_steal(spmc, &newval);
		if (ret) {
			arena_stderr("%s:%d error %d\n", __func__, __LINE__, ret);
			return 1;
		}

		if (newval != i) {
			arena_stderr("%s:%d expected %d found %llu\n", __func__, __LINE__, i, newval);
			return 1;
		}
	}

	spmc_destroy(spmc);

	return 0;
}
