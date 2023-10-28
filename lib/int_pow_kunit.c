#include <kunit/test.h>
#include <linux/errno.h>

#include <linux/math.h>

static void should_return_0(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, int_pow(0, 1), 0);
	KUNIT_EXPECT_EQ(test, int_pow(0, 0), 0);
	KUNIT_EXPECT_EQ(test, int_pow(2, 0), 0);
}

static void should_return_9(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, int_pow(3, 2), 9);
	KUNIT_EXPECT_EQ(test, int_pow(-3, 2), 9);
}

static struct kunit_case int_pow_test_case[] = {
	KUNIT_CASE(should_return_0),
	KUNIT_CASE(should_return_9),
	{ /* sentinel */ }
};

static struct kunit_suite int_pow_test = {
	.name = "int_pow",
	.test_cases = int_pow_test_case
};

kunit_test_suite(int_pow_test);

MODULE_AUTHOR("Guilherme Giacomo Simoes <trintaeoitogc@gmail.com>");
MODULE_LICENSE("GPL");
