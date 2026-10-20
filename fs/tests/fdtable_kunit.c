// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/fdtable.h>
#include <linux/file.h>

static void test_alloc_fdtable(struct kunit *test)
{
	struct fdtable *fdt;
	unsigned int slots = 64;

	fdt = alloc_fdtable(slots);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fdt);

	/* Check that max_fds is set correctly and is >= slots */
	KUNIT_EXPECT_GE(test, fdt->max_fds, slots);

	/* Check that fd is allocated */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fdt->fd);

	/*
	 * Check dynamic object size of fdt->fd if compiler supports
	 * __counted_by_ptr.
	 */
#ifdef CONFIG_CC_HAS_COUNTED_BY_PTR
	KUNIT_EXPECT_EQ(test, __struct_size(fdt->fd),
			fdt->max_fds * sizeof(struct file *));
#endif

	__free_fdtable(fdt);
}

static void test_dup_fd(struct kunit *test)
{
	struct files_struct *newf;
	struct fdtable *fdt;

	newf = dup_fd(&init_files, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, newf);

	fdt = rcu_dereference_raw(newf->fdt);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fdt);

	/* Check that max_fds is set correctly and is >= NR_OPEN_DEFAULT */
	KUNIT_EXPECT_GE(test, fdt->max_fds, NR_OPEN_DEFAULT);

	/* Check that fd is allocated */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fdt->fd);

	/*
	 * Check dynamic object size of fdt->fd if compiler supports
	 * __counted_by_ptr.
	 */
#ifdef CONFIG_CC_HAS_COUNTED_BY_PTR
	KUNIT_EXPECT_EQ(test, __struct_size(fdt->fd),
			fdt->max_fds * sizeof(struct file *));
#endif

	put_files_struct(newf);
}

static struct kunit_case fdtable_test_cases[] = {
	KUNIT_CASE(test_alloc_fdtable),
	KUNIT_CASE(test_dup_fd),
	{}
};

static struct kunit_suite fdtable_test_suite = {
	.name = "fdtable",
	.test_cases = fdtable_test_cases,
};

kunit_test_suite(fdtable_test_suite);
