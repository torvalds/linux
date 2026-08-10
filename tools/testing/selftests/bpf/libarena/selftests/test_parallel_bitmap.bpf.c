// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause

#include <bpf_atomic.h>

#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/bitmap.h>

#define TEST_BITMAP_THREADS	2
#define TEST_BITMAP_BITS	(2 * BITS_PER_LONG_LONG)
#define TEST_BITMAP_SYNC_SPINS	BPF_MAX_LOOPS
#define TEST_BITMAP_ITERS	10 * 1000 * 1000

static struct arena_bitmap __arena *bitmap;
static volatile u64 started;
static volatile bool test_abort;

/*
 * The test needs cmpxchg atomics on arena memory.
 */
#if defined(ENABLE_ATOMICS_TESTS) &&				  \
	(defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
	 defined(__TARGET_ARCH_s390) || \
	 defined(__TARGET_ARCH_powerpc) ||				  \
	 (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64))
static bool bitmap_tests_enabled(void)
{
	return true;
}
#else
static bool bitmap_tests_enabled(void)
{
	return false;
}
#endif

__weak
int bitmap_wait_for_start(void)
{
	u64 i;

	__sync_fetch_and_add(&started, 1);

	for (i = zero; i < TEST_BITMAP_SYNC_SPINS && can_loop; i++) {
		if (test_abort)
			return -EINTR;
		if (smp_load_acquire(&started) >= TEST_BITMAP_THREADS)
			return 0;
	}

	test_abort = true;
	return -ETIMEDOUT;
}

/*
 * The test makes sure writes don't clobber each other by overwriting
 * the same word. One thread always writes on even bits, the other on
 * odds. Both should be able to operate on the bitmap oblivious of the
 * other's operations.
 */
__weak
int bitmap_test_bit_sequence(u32 bit)
{
	if (bmp_test_and_clear_bit(bit, bitmap))
		return -EINVAL;

	if (bmp_test_and_set_bit(bit, bitmap))
		return -EINVAL;
	if (!bmp_test_bit(bit, bitmap))
		return -EINVAL;

	if (!bmp_test_and_set_bit(bit, bitmap))
		return -EINVAL;
	if (!bmp_test_bit(bit, bitmap))
		return -EINVAL;

	if (!bmp_test_and_clear_bit(bit, bitmap))
		return -EINVAL;
	if (bmp_test_bit(bit, bitmap))
		return -EINVAL;

	if (bmp_test_and_clear_bit(bit, bitmap))
		return -EINVAL;

	bmp_set_bit(bit, bitmap);
	if (!bmp_test_bit(bit, bitmap))
		return -EINVAL;

	bmp_clear_bit(bit, bitmap);
	if (bmp_test_bit(bit, bitmap))
		return -EINVAL;

	bmp_set_bit(bit, bitmap);
	if (!bmp_test_bit(bit, bitmap))
		return -EINVAL;

	return 0;

}

static void bitmap_test_reset_single(int parity)
{
	u32 bit;

	for (bit = parity; bit < TEST_BITMAP_BITS && can_loop; bit += 2)
		bmp_clear_bit(bit, bitmap);

}

static int bitmap_test_common_single(int parity)
{
	u32 bit;
	int ret;

	for (bit = parity; bit < TEST_BITMAP_BITS && can_loop; bit += 2) {
		if (test_abort)
			return -EINTR;

		ret = bitmap_test_bit_sequence(bit);
		if (ret) {
			test_abort = true;
			return ret;
		}
	}

	return 0;
}

static int bitmap_test_common(int parity)
{
	int ret;
	u32 i;

	arena_subprog_init();

	ret = bitmap_wait_for_start();
	if (ret)
		return ret;

	for (i = zero; i < TEST_BITMAP_ITERS && can_loop; i++) {
		ret = bitmap_test_common_single(parity);
		if (ret)
			return ret;

		if (test_abort)
			break;

		bitmap_test_reset_single(parity);
	}

	return 0;
}

SEC("syscall") int parallel_test_bitmap__enabled(void)
{
	return bitmap_tests_enabled() ? 0 : -EOPNOTSUPP;
}

SEC("syscall") int parallel_test_bitmap__init(void)
{
	bitmap = bmp_alloc(TEST_BITMAP_BITS);
	if (!bitmap)
		return -ENOMEM;

	return 0;
}

SEC("syscall") int parallel_test_bitmap__fini(void)
{
	int ret = 0;

	if (!bitmap)
		return -EINVAL;

	bmp_free(bitmap);
	bitmap = NULL;

	return ret;
}

SEC("syscall") int parallel_test_bitmap__0(void)
{
	return bitmap_test_common(0);
}

SEC("syscall") int parallel_test_bitmap__1(void)
{
	return bitmap_test_common(1);
}
