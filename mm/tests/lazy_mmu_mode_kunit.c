// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/pgtable.h>

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static void expect_not_active(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test, is_lazy_mmu_mode_active());
}

static void expect_active(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, is_lazy_mmu_mode_active());
}

static void lazy_mmu_mode_active(struct kunit *test)
{
	expect_not_active(test);

	lazy_mmu_mode_enable();
	expect_active(test);

	{
		/* Nested section */
		lazy_mmu_mode_enable();
		expect_active(test);

		lazy_mmu_mode_disable();
		expect_active(test);
	}

	{
		/* Paused section */
		lazy_mmu_mode_pause();
		expect_not_active(test);

		{
			/* No effect (paused) */
			lazy_mmu_mode_enable();
			expect_not_active(test);

			lazy_mmu_mode_disable();
			expect_not_active(test);

			lazy_mmu_mode_pause();
			expect_not_active(test);

			lazy_mmu_mode_resume();
			expect_not_active(test);
		}

		lazy_mmu_mode_resume();
		expect_active(test);
	}

	lazy_mmu_mode_disable();
	expect_not_active(test);
}

static struct kunit_case lazy_mmu_mode_test_cases[] = {
	KUNIT_CASE(lazy_mmu_mode_active),
	{}
};

static struct kunit_suite lazy_mmu_mode_test_suite = {
	.name = "lazy_mmu_mode",
	.test_cases = lazy_mmu_mode_test_cases,
};
kunit_test_suite(lazy_mmu_mode_test_suite);

MODULE_DESCRIPTION("Tests for the lazy MMU mode");
MODULE_LICENSE("GPL");
