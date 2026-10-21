#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/bitmap.h>

#define TEST_BITS	(2 * BITS_PER_LONG_LONG)
#define TEST_WORDS	BITS_TO_LONG_LONGS(TEST_BITS)
#define MID_BIT		(BITS_PER_LONG_LONG + 1)
#define LAST_BIT	(TEST_BITS - 1)

static void test_bmp_setall(struct arena_bitmap __arena *bmp)
{
	volatile u32 i;

	for (i = zero; i < TEST_WORDS && can_loop; i++)
		bmp->bits[i] = ~0ULL;
}

SEC("syscall")
__weak int test_bitmap_alloc_free(void)
{
	struct arena_bitmap __arena *bmp;

	bmp = bmp_alloc(TEST_BITS);
	if (!bmp)
		return -ENOMEM;

	if (!bmp_empty(TEST_BITS, bmp))
		goto err;

	__bmp_set_bit(LAST_BIT, bmp);
	if (!bmp_test_bit(LAST_BIT, bmp))
		goto err;

	__bmp_clear_bit(LAST_BIT, bmp);
	if (bmp_test_bit(LAST_BIT, bmp))
		goto err;

	bmp_free(bmp);
	return 0;

err:
	bmp_free(bmp);
	return -EINVAL;
}

SEC("syscall")
__weak int test_bitmap_bit_ops(void)
{
	struct arena_bitmap __arena *bmp;

	bmp = bmp_alloc(TEST_BITS);
	if (!bmp)
		return -ENOMEM;

	__bmp_set_bit(0, bmp);
	if (!bmp_test_bit(0, bmp))
		goto err;

	__bmp_set_bit(MID_BIT, bmp);
	if (!bmp_test_bit(MID_BIT, bmp))
		goto err;

	__bmp_set_bit(LAST_BIT, bmp);
	if (!bmp_test_bit(LAST_BIT, bmp))
		goto err;

	if (bmp_test_bit(MID_BIT - 1, bmp))
		goto err;

	__bmp_clear_bit(MID_BIT, bmp);
	if (bmp_test_bit(MID_BIT, bmp))
		goto err;

	if (!bmp_test_bit(0, bmp))
		goto err;

	if (!bmp_test_bit(LAST_BIT, bmp))
		goto err;

	__bmp_clear_bit(0, bmp);
	__bmp_clear_bit(LAST_BIT, bmp);
	if (!bmp_empty(TEST_BITS, bmp))
		goto err;

	if (bmp->bits[0])
		goto err;

	if (bmp->bits[1])
		goto err;

	bmp_free(bmp);
	return 0;

err:
	bmp_free(bmp);
	return -EINVAL;
}

static bool test_bitmap_test_and_clear_single(struct arena_bitmap __arena *bmp, size_t ind)
{
	if (bmp_test_and_clear_bit(ind, bmp))
		return false;

	__bmp_set_bit(ind, bmp);

	if (!bmp_test_and_clear_bit(ind, bmp))
		return false;

	if (bmp_test_bit(ind, bmp))
		return false;

	if (bmp_test_and_clear_bit(ind, bmp))
		return false;

	return true;
}

static bool test_bitmap_test_and_set_single(struct arena_bitmap __arena *bmp, size_t ind)
{
	if (bmp_test_and_set_bit(ind, bmp))
		return false;

	if (!bmp_test_and_set_bit(ind, bmp))
		return false;

	if (!bmp_test_bit(ind, bmp))
		return false;

	__bmp_clear_bit(ind, bmp);

	if (bmp_test_and_set_bit(ind, bmp))
		return false;

	return true;
}

SEC("syscall")
__weak int test_bitmap_test_and_clear_bit(void)
{
	struct arena_bitmap __arena *bmp;

	bmp = bmp_alloc(TEST_BITS);
	if (!bmp)
		return -ENOMEM;

	if (!test_bitmap_test_and_clear_single(bmp, 0))
		goto err;

	if (!test_bitmap_test_and_clear_single(bmp, MID_BIT))
		goto err;

	if (!test_bitmap_test_and_clear_single(bmp, LAST_BIT))
		goto err;

	if (!bmp_empty(TEST_BITS, bmp))
		goto err;

	bmp_free(bmp);
	return 0;

err:
	bmp_free(bmp);
	return -EINVAL;
}

SEC("syscall")
__weak int test_bitmap_test_and_set_bit(void)
{
	struct arena_bitmap __arena *bmp;

	bmp = bmp_alloc(TEST_BITS);
	if (!bmp)
		return -ENOMEM;

	if (!test_bitmap_test_and_set_single(bmp, 0))
		goto err;

	if (!test_bitmap_test_and_set_single(bmp, MID_BIT))
		goto err;

	if (!test_bitmap_test_and_set_single(bmp, LAST_BIT))
		goto err;

	bmp_free(bmp);
	return 0;

err:
	bmp_free(bmp);
	return -EINVAL;
}


SEC("syscall")
__weak int test_bitmap_and(void)
{
	struct arena_bitmap __arena *src1 = NULL, *src2 = NULL, *dst = NULL;

	src1 = bmp_alloc(TEST_BITS);
	src2 = bmp_alloc(TEST_BITS);
	dst = bmp_alloc(TEST_BITS);
	if (!src1 || !src2 || !dst)
		goto err;

	test_bmp_setall(dst);

	__bmp_set_bit(0, src1);
	__bmp_set_bit(MID_BIT, src1);
	__bmp_set_bit(LAST_BIT, src1);

	__bmp_set_bit(MID_BIT, src2);
	__bmp_set_bit(LAST_BIT, src2);

	bmp_and(TEST_BITS, dst, src1, src2);

	if (bmp_test_bit(0, dst))
		goto err;
	if (!bmp_test_bit(MID_BIT, dst))
		goto err;
	if (!bmp_test_bit(LAST_BIT, dst))
		goto err;

	if (dst->bits[0])
		goto err;
	if (dst->bits[1] != (BIT_MASK(MID_BIT) | BIT_MASK(LAST_BIT)))
		goto err;

	bmp_free(src1);
	bmp_free(src2);
	bmp_free(dst);
	return 0;

err:
	bmp_free(src1);
	bmp_free(src2);
	bmp_free(dst);
	return -EINVAL;
}

SEC("syscall")
__weak int test_bitmap_or(void)
{
	struct arena_bitmap __arena *src1 = NULL, *src2 = NULL, *dst = NULL;

	src1 = bmp_alloc(TEST_BITS);
	src2 = bmp_alloc(TEST_BITS);
	dst = bmp_alloc(TEST_BITS);
	if (!src1 || !src2 || !dst)
		goto err;

	test_bmp_setall(dst);

	__bmp_set_bit(0, src1);
	__bmp_set_bit(LAST_BIT, src1);

	__bmp_set_bit(MID_BIT, src2);
	__bmp_set_bit(LAST_BIT, src2);

	bmp_or(TEST_BITS, dst, src1, src2);

	if (!bmp_test_bit(0, dst))
		goto err;
	if (!bmp_test_bit(MID_BIT, dst))
		goto err;
	if (!bmp_test_bit(LAST_BIT, dst))
		goto err;

	if (dst->bits[0] != BIT_MASK(0))
		goto err;
	if (dst->bits[1] != (BIT_MASK(MID_BIT) | BIT_MASK(LAST_BIT)))
		goto err;

	bmp_free(src1);
	bmp_free(src2);
	bmp_free(dst);
	return 0;

err:
	bmp_free(src1);
	bmp_free(src2);
	bmp_free(dst);
	return -EINVAL;
}

SEC("syscall")
__weak int test_bitmap_subset(void)
{
	struct arena_bitmap __arena *big = NULL, *small = NULL;

	big = bmp_alloc(TEST_BITS);
	small = bmp_alloc(TEST_BITS);
	if (!big || !small)
		goto err;

	if (!bmp_subset(TEST_BITS, big, small))
		goto err;

	__bmp_set_bit(0, small);
	if (bmp_subset(TEST_BITS, big, small))
		goto err;

	__bmp_set_bit(0, big);
	if (!bmp_subset(TEST_BITS, big, small))
		goto err;

	__bmp_set_bit(LAST_BIT, small);
	if (bmp_subset(TEST_BITS, big, small))
		goto err;

	__bmp_set_bit(LAST_BIT, big);
	__bmp_set_bit(MID_BIT, big);
	if (!bmp_subset(TEST_BITS, big, small))
		goto err;

	if (bmp_subset(TEST_BITS, small, big))
		goto err;

	bmp_free(big);
	bmp_free(small);
	return 0;

err:
	bmp_free(big);
	bmp_free(small);
	return -EINVAL;

}

SEC("syscall")
__weak int test_bitmap_intersects(void)
{
	struct arena_bitmap __arena *arg1 = NULL, *arg2 = NULL;

	arg1 = bmp_alloc(TEST_BITS);
	arg2 = bmp_alloc(TEST_BITS);
	if (!arg1 || !arg2)
		goto err;

	if (bmp_intersects(TEST_BITS, arg1, arg2))
		goto err;

	__bmp_set_bit(0, arg1);
	__bmp_set_bit(MID_BIT, arg2);
	if (bmp_intersects(TEST_BITS, arg1, arg2))
		goto err;

	__bmp_set_bit(LAST_BIT, arg1);
	__bmp_set_bit(LAST_BIT, arg2);
	if (!bmp_intersects(TEST_BITS, arg1, arg2))
		goto err;

	bmp_free(arg1);
	bmp_free(arg2);
	return 0;

err:
	bmp_free(arg1);
	bmp_free(arg2);
	return -EINVAL;
}

SEC("syscall")
__weak int test_bitmap_copy(void)
{
	struct arena_bitmap __arena *arg1 = NULL, *arg2 = NULL;

	arg1 = bmp_alloc(TEST_BITS);
	arg2 = bmp_alloc(TEST_BITS);
	if (!arg1 || !arg2)
		goto err;

	__bmp_set_bit(0, arg1);
	__bmp_set_bit(MID_BIT, arg1);

	/* Make sure those get overwritten. */
	__bmp_set_bit(1, arg2);
	__bmp_set_bit(MID_BIT + 2, arg2);

	bmp_copy(TEST_BITS, arg2, arg1);

	/* Bitmaps are equal if a subset of each other. */
	if (!bmp_subset(TEST_BITS, arg1, arg2) ||
	    !bmp_subset(TEST_BITS, arg2, arg1))
		goto err;

	bmp_free(arg1);
	bmp_free(arg2);
	return 0;

err:
	bmp_free(arg1);
	bmp_free(arg2);
	return -EINVAL;
}
